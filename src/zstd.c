/* Copyright (C) 2026 Graeme Smecher
 *
 ***************************************************************************
 *
 * This file is part of the GetData project.
 *
 * GetData is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * GetData is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with GetData; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "internal.h"

#ifdef HAVE_ZSTD_H
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#endif

/* The zstd encoding stores data as independently compressed zstd frames,
 * appended sequentially.  Each frame records its decompressed size in the
 * standard zstd frame header via ZSTD_CCtx_setPledgedSrcSize().
 *
 * Writes are in-place and append-only for forward writes.  Backward writes
 * into already-committed frames trigger a rewrite: the affected portion of the
 * file is decompressed, patched, recompressed, and written back in-place
 * (truncating the file at the rewrite point first).  On open, frame headers
 * are scanned to build an in-memory index for random-access reads.  Each read
 * decompresses only the frame(s) covering the requested byte range.
 *
 * All frames record their content size via ZSTD_CCtx_setPledgedSrcSize().
 * Scanning stops at the first frame with an unknown content size, which
 * indicates corruption or a truncated write.
 *
 * The write path uses ZSTD_compressStream2() with ZSTD_c_stableInBuffer.
 * Incoming data is copied into write_buf (the stable input window), and the
 * compressor's ZSTD_inBuffer always points to write_buf with an advancing
 * pos but a never-changing src.  This satisfies stableInBuffer's requirement
 * that src remain unmodified between calls, while allowing zstd to skip its
 * own internal window copy.  Compressed output is written to z->fd as zstd
 * produces it (ZSTD_e_continue), rather than accumulating until frame close.
 * Frames are delimited by pledged source size: when a frame is closed,
 * ZSTD_CCtx_setPledgedSrcSize is called with the exact byte count accumulated
 * in write_buf, then the frame is flushed with ZSTD_e_end.  Full and partial
 * frames are closed the same way.
 *
 * Note: ZSTD_getFrameHeader, ZSTD_frameHeader, and ZSTD_c_stableInBuffer
 * require ZSTD_STATIC_LINKING_ONLY, which ties the build to the specific
 * libzstd version it was compiled against.
 */

/* Default frame size for compression (128 KiB uncompressed) */
#define GD_ZSTD_FRAME_SIZE (128 * 1024)

/* Compression level (zstd README.md includes benchmarks) */
#define GD_ZSTD_COMP_LEVEL 1


struct gd_zstd_frame {
  uint64_t comp_offset;   /* byte offset of compressed frame in file */
  uint32_t comp_size;     /* compressed size of this frame */
  uint32_t decomp_size;   /* decompressed size of this frame */
};

struct gd_zstddata {
  int fd;                 /* underlying file descriptor */

  /* Frame index */
  struct gd_zstd_frame *frames;
  uint32_t n_frames;
  uint32_t frames_alloc;
  uint64_t total_decomp;  /* total committed decompressed size in bytes */

  /* Read state */
  ZSTD_DCtx *dctx;
  char *read_buf;         /* decompressed frame cache */
  uint32_t read_buf_size;
  int32_t cached_frame;   /* which frame is cached, -1 = none */

  /* Write state */
  ZSTD_CCtx *cctx;
  char *write_buf;        /* stable input window: data copied here before feeding to compressor */
  char *out_buf;          /* compressed output buffer, size = ZSTD_compressBound(frame_size) */
  uint32_t out_buf_size;
  uint32_t frame_size;    /* target uncompressed frame size */
  uint32_t frame_used;    /* bytes copied into write_buf for the current open frame */
  uint64_t write_offset;  /* file offset for next compressed write */

  size_t last_error;      /* last ZSTD error code */
};

/* Add a frame to the in-memory index */
static int _GD_ZstdAddFrame(struct gd_zstddata *z, uint64_t comp_offset,
    uint32_t comp_size, uint32_t decomp_size)
{
  if (z->n_frames >= z->frames_alloc) {
    uint32_t new_alloc = z->frames_alloc ? z->frames_alloc * 2 : 64;
    struct gd_zstd_frame *new_frames = realloc(z->frames,
        new_alloc * sizeof(*new_frames));
    if (new_frames == NULL)
      return -1;
    z->frames = new_frames;
    z->frames_alloc = new_alloc;
  }

  z->frames[z->n_frames].comp_offset = comp_offset;
  z->frames[z->n_frames].comp_size = comp_size;
  z->frames[z->n_frames].decomp_size = decomp_size;
  z->n_frames++;
  z->total_decomp += decomp_size;
  return 0;
}

/* Scan frame headers to build the in-memory index.  Each frame's compressed
 * size and decompressed size are determined from its header using library
 * functions -- no decompression is performed.  Returns 0 on success. */
static int _GD_ZstdScanFrames(struct gd_zstddata *z)
{
  off64_t pos, file_size;
  char *probe = NULL;
  size_t probe_alloc = 0;

  dtrace("%p", z);

  file_size = lseek64(z->fd, 0, SEEK_END);
  if (file_size <= 0) {
    dreturn("%i", (file_size == 0) ? 0 : -1);
    return (file_size == 0) ? 0 : -1;
  }

  if (lseek64(z->fd, 0, SEEK_SET) == -1) {
    dreturn("%i", -1);
    return -1;
  }

  z->n_frames = 0;
  z->total_decomp = 0;
  pos = 0;

  while (pos < file_size) {
    uint8_t hdr[ZSTD_FRAMEHEADERSIZE_MAX];
    ZSTD_frameHeader zfh;
    size_t remaining, hdr_read, ret;
    size_t comp_size;

    /* Read enough for the frame header */
    remaining = (size_t)(file_size - pos);
    hdr_read = ZSTD_FRAMEHEADERSIZE_MAX;
    if (hdr_read > remaining)
      hdr_read = remaining;

    if (lseek64(z->fd, pos, SEEK_SET) == -1) {
      free(probe);
      dreturn("%i", -1);
      return -1;
    }
    if (read(z->fd, hdr, hdr_read) != (ssize_t)hdr_read) {
      free(probe);
      dreturn("%i", -1);
      return -1;
    }

    /* Decode frame header */
    ret = ZSTD_getFrameHeader(&zfh, hdr, hdr_read);
    if (ZSTD_isError(ret))
      break;
    if (ret > 0)
      break;  /* need more data than available -- truncated frame */

    if (zfh.frameType == ZSTD_skippableFrame) {
      /* Skippable frame: header tells us everything */
      pos += (off64_t)(zfh.headerSize + zfh.frameContentSize);
    } else {
      /* Data frame: use ZSTD_findFrameCompressedSize to determine
       * compressed size.  This needs the full compressed frame in the
       * buffer.  We use ZSTD_compressBound(contentSize) as a
       * conservative upper bound; it over-estimates for well-compressed
       * data but is always >= the actual compressed size.  Falls back
       * to reading everything remaining if content size is unknown or
       * if ZSTD_compressBound fails. */
      size_t probe_size;

      /* We are the only producer: all frames have a known content size.
       * An unknown content size means a corrupt or foreign file -- stop. */
      if (zfh.frameContentSize == ZSTD_CONTENTSIZE_UNKNOWN ||
          zfh.frameContentSize == ZSTD_CONTENTSIZE_ERROR)
        break;

      probe_size = ZSTD_compressBound((size_t)zfh.frameContentSize);
      if (ZSTD_isError(probe_size) || probe_size > remaining)
        probe_size = remaining;

      if (probe_size > probe_alloc) {
        char *new_probe = realloc(probe, probe_size);
        if (new_probe == NULL) {
          free(probe);
          dreturn("%i", -1);
          return -1;
        }
        probe = new_probe;
        probe_alloc = probe_size;
      }

      if (lseek64(z->fd, pos, SEEK_SET) == -1) {
        free(probe);
        dreturn("%i", -1);
        return -1;
      }
      if (read(z->fd, probe, probe_size) != (ssize_t)probe_size) {
        free(probe);
        dreturn("%i", -1);
        return -1;
      }

      comp_size = ZSTD_findFrameCompressedSize(probe, probe_size);

      if (ZSTD_isError(comp_size) || comp_size > UINT32_MAX ||
          zfh.frameContentSize > UINT32_MAX)
        break;

      if (zfh.frameContentSize > 0) {
        if (_GD_ZstdAddFrame(z, (uint64_t)pos, (uint32_t)comp_size,
              (uint32_t)zfh.frameContentSize)) {
          free(probe);
          dreturn("%i", -1);
          return -1;
        }
      }

      pos += (off64_t)comp_size;
    }
  }

  free(probe);
  dreturn("%i", 0);
  return 0;
}

/* Compress one chunk of input and write the output to fd.
 * Returns the zstd return value (0 = done, >0 = more output pending),
 * or -1 on error. */
static int _GD_ZstdWriteOut(struct gd_zstddata *z, ZSTD_inBuffer *in,
    ZSTD_EndDirective directive)
{
  ZSTD_outBuffer out;
  size_t r;

  out.dst  = z->out_buf;
  out.size = z->out_buf_size;
  out.pos  = 0;

  r = ZSTD_compressStream2(z->cctx, &out, in, directive);
  if (ZSTD_isError(r)) {
    z->last_error = r;
    return -1;
  }

  if (out.pos > 0) {
    if (write(z->fd, z->out_buf, out.pos) != (ssize_t)out.pos)
      return -1;
    z->write_offset += out.pos;
  }

  return (int)r;
}

/* Close the current open frame: pledge the exact size, replay write_buf
 * through the compressor, flush with ZSTD_e_end, update the frame index,
 * and reset frame_used.  Returns 0 on success. */
static int _GD_ZstdEndFrame(struct gd_zstddata *z)
{
  ZSTD_inBuffer in;
  uint64_t frame_comp_start;
  uint32_t decomp_size;
  size_t r;
  int rc;

  dtrace("%p", z);

  if (z->frame_used == 0) {
    dreturn("%i", 0);
    return 0;
  }

  decomp_size = z->frame_used;
  frame_comp_start = z->write_offset;

  /* Now that we know the exact frame size, reset and re-open with the correct
   * pledge.  write_buf holds all the data so we replay it in one pass. */
  ZSTD_CCtx_reset(z->cctx, ZSTD_reset_session_only);

  r = ZSTD_CCtx_setPledgedSrcSize(z->cctx, (unsigned long long)decomp_size);
  if (ZSTD_isError(r)) {
    z->last_error = r;
    dreturn("%i", -1);
    return -1;
  }

  /* Feed write_buf in its entirety with ZSTD_e_continue, then close with
   * ZSTD_e_end.  Two separate loops ensure ZSTD_e_end is always issued. */
  in.src  = z->write_buf;
  in.size = decomp_size;
  in.pos  = 0;

  /* Position once at the start of this frame; subsequent writes are sequential */
  if (lseek64(z->fd, (off64_t)z->write_offset, SEEK_SET) == -1) {
    dreturn("%i", -1);
    return -1;
  }

  /* Feed phase */
  while (in.pos < in.size) {
    if (_GD_ZstdWriteOut(z, &in, ZSTD_e_continue) < 0) {
      dreturn("%i", -1);
      return -1;
    }
  }

  /* Flush phase */
  do {
    rc = _GD_ZstdWriteOut(z, &in, ZSTD_e_end);
    if (rc < 0) {
      dreturn("%i", -1);
      return -1;
    }
  } while (rc > 0);

  uint32_t comp_size = (uint32_t)(z->write_offset - frame_comp_start);
  if (_GD_ZstdAddFrame(z, frame_comp_start, comp_size, decomp_size)) {
    dreturn("%i", -1);
    return -1;
  }

  z->frame_used = 0;

  dreturn("%i", 0);
  return 0;
}

/* Returns the frame index containing byte_offset, and sets *frame_start_out
 * to the decompressed byte offset where that frame begins.
 * Returns -1 (and leaves *frame_start_out unchanged) if past EOF. */
static int32_t _GD_ZstdLocateFrame(const struct gd_zstddata *z,
    uint64_t byte_offset, uint64_t *frame_start_out)
{
  uint64_t decomp_pos = 0;
  uint32_t i;

  for (i = 0; i < z->n_frames; ++i) {
    if (byte_offset < decomp_pos + z->frames[i].decomp_size) {
      *frame_start_out = decomp_pos;
      return (int32_t)i;
    }
    decomp_pos += z->frames[i].decomp_size;
  }

  return -1;
}

/* Rewrite the file from the first affected frame onward, overlaying new data.
 * Used for backward writes into already-committed (compressed) frames.
 *
 * Decompresses all frames from the first affected one onward, merges any
 * in-progress frame data (frame_used bytes fed so far but not yet committed),
 * overlays the new data, recompresses, and writes back in-place (truncating
 * the file at the rewrite point first).
 *
 * Returns the number of samples written, or -1 on error. */
static ssize_t _GD_ZstdRewriteFrom(struct gd_zstddata *z,
    struct gd_raw_file_ *file gd_unused_, uint64_t write_pos,
    const char *data, size_t data_len, size_t sample_size)
{
  int32_t first_frame;
  uint64_t frame_start, tail_decomp_size, total_size;
  uint64_t trunc_point, decomp_off;
  uint64_t remaining, recomp_off;
  char *decomp_buf = NULL;
  uint32_t i;

  dtrace("%p, %p, %" PRIu64 ", %p, %" PRIuSIZE ", %" PRIuSIZE,
      z, file, write_pos, data, data_len, sample_size);

  /* Ensure we have a decompression context (may not exist if file was
   * opened write-only) */
  if (z->dctx == NULL) {
    z->dctx = ZSTD_createDCtx();
    if (z->dctx == NULL) {
      dreturn("%i", -1);
      return -1;
    }
  }

  /* Find the first committed frame affected by this write */
  frame_start = 0;
  first_frame = _GD_ZstdLocateFrame(z, write_pos, &frame_start);
  if (first_frame < 0) {
    dreturn("%i", -1);
    return -1;
  }

  /* Compressed byte offset of first_frame -- everything before this is kept */
  trunc_point = z->frames[first_frame].comp_offset;

  /* Total decompressed bytes from first_frame through end of committed data */
  tail_decomp_size = z->total_decomp - frame_start;

  /* Callers flush any in-progress frame before calling here */
  total_size = tail_decomp_size;

  /* Extend if the write goes past current end */
  if (write_pos + data_len > frame_start + total_size)
    total_size = (write_pos + data_len) - frame_start;

  decomp_buf = malloc((size_t)total_size);
  if (decomp_buf == NULL) {
    dreturn("%i", -1);
    return -1;
  }
  memset(decomp_buf, 0, (size_t)total_size);

  /* Decompress committed frames from first_frame onward into buffer */
  decomp_off = 0;
  for (i = (uint32_t)first_frame; i < z->n_frames; ++i) {
    struct gd_zstd_frame *f = &z->frames[i];
    char *frame_comp;
    size_t result;

    frame_comp = malloc(f->comp_size);
    if (frame_comp == NULL)
      goto error;

    if (lseek64(z->fd, (off64_t)f->comp_offset, SEEK_SET) == -1) {
      free(frame_comp);
      goto error;
    }
    if (read(z->fd, frame_comp, f->comp_size) != (ssize_t)f->comp_size) {
      free(frame_comp);
      goto error;
    }

    result = ZSTD_decompressDCtx(z->dctx, decomp_buf + decomp_off,
        f->decomp_size, frame_comp, f->comp_size);
    free(frame_comp);

    if (ZSTD_isError(result)) {
      z->last_error = result;
      goto error;
    }

    decomp_off += f->decomp_size;
  }

  /* Overlay the new write data */
  memcpy(decomp_buf + (write_pos - frame_start), data, data_len);

  /* Truncate the file at the rewrite point and recompress from there */
  if (gd_truncate(z->fd, (off64_t)trunc_point))
    goto error;

  /* Trim the frame index back to first_frame */
  z->total_decomp = frame_start;
  z->n_frames = (uint32_t)first_frame;
  z->write_offset = trunc_point;
  z->frame_used = 0;

  /* Recompress the modified data as new frames via the streaming path */
  remaining = total_size;
  recomp_off = 0;
  while (remaining > 0) {
    uint32_t chunk = (uint32_t)(remaining < z->frame_size ?
        remaining : z->frame_size);

    memcpy(z->write_buf + z->frame_used, decomp_buf + recomp_off, chunk);
    z->frame_used += chunk;

    if (_GD_ZstdEndFrame(z))
      goto error;

    recomp_off += chunk;
    remaining -= chunk;
  }

  free(decomp_buf);
  decomp_buf = NULL;

  z->cached_frame = -1;

  dreturn("%" PRIdSIZE, (ssize_t)(data_len / sample_size));
  return (ssize_t)(data_len / sample_size);

error:
  free(decomp_buf);
  dreturn("%i", -1);
  return -1;
}

/* Encoding parameters parsed from enc_data and passed from _GD_ZstdName to
 * _GD_ZstdOpen via file->edata.  The framework frees edata when a name-only
 * probe is performed without a subsequent open (e.g. _GD_ProbeChunk). */
struct gd_zstd_params {
  uint32_t frame_size;  /* target uncompressed frame size in bytes */
};

/* Name function: delegates to _GD_GenericName, then parses enc_data to extract
 * zstd encoding parameters, stashed on file->edata for _GD_ZstdOpen. */
int _GD_ZstdName(DIRFILE *restrict D, const char *restrict enc_data,
    struct gd_raw_file_ *restrict file, const char *restrict base, int temp,
    int resolv)
{
  int ret;
  struct gd_zstd_params *params;

  dtrace("%p, \"%s\", %p, \"%s\", %i, %i", D, enc_data, file, base, temp,
      resolv);

  ret = _GD_GenericName(D, enc_data, file, base, temp, resolv);
  if (ret) {
    dreturn("%i", ret);
    return ret;
  }

  /* Stash parsed parameters for _GD_ZstdOpen to pick up */
  if (file->edata == NULL) {
    params = malloc(sizeof(*params));
    if (params == NULL) {
      dreturn("%i", -1);
      return -1;
    }

    params->frame_size = GD_ZSTD_FRAME_SIZE;

    if (enc_data != NULL && enc_data[0] != '\0') {
      char *endptr;
      unsigned long val = strtoul(enc_data, &endptr, 10);
      if (*endptr == '\0' && val > 0 && val <= UINT32_MAX)
        params->frame_size = (uint32_t)val;
    }

    file->edata = params;
  }

  dreturn("%i", 0);
  return 0;
}

int _GD_ZstdOpen(int fd, struct gd_raw_file_ *file,
    gd_type_t data_type gd_unused_, int swap gd_unused_, unsigned int mode)
{
  struct gd_zstddata *z;
  uint32_t frame_size = GD_ZSTD_FRAME_SIZE;

  dtrace("%i, %p, <unused>, <unused>, 0x%X", fd, file, mode);

  /* Pick up encoding parameters from _GD_ZstdName */
  if (file->edata != NULL) {
    struct gd_zstd_params *params = (struct gd_zstd_params *)file->edata;
    frame_size = params->frame_size;
    free(params);
    file->edata = NULL;
  }

  z = calloc(1, sizeof(*z));
  if (z == NULL) {
    dreturn("%i", 1);
    return 1;
  }

  z->fd = -1;
  z->cached_frame = -1;
  z->frame_size = frame_size;

  if (mode & GD_FILE_TEMP) {
    z->fd = _GD_MakeTempFile(file->D, fd, file->name);
  } else if (mode & GD_FILE_WRITE) {
    z->fd = gd_OpenAt(file->D, fd, file->name,
        O_RDWR | O_CREAT | O_BINARY, 0666);
  } else {
    z->fd = gd_OpenAt(file->D, fd, file->name, O_RDONLY | O_BINARY, 0666);
  }

  if (z->fd < 0) {
    free(z);
    dreturn("%i", 1);
    return 1;
  }

  /* Scan existing frames to build the in-memory index */
  if (_GD_ZstdScanFrames(z)) {
    close(z->fd);
    free(z->frames);
    free(z);
    dreturn("%i", 1);
    return 1;
  }

  if (mode & GD_FILE_WRITE) {
    size_t bound;
    size_t r;

    z->cctx = ZSTD_createCCtx();
    if (z->cctx == NULL) {
      close(z->fd);
      free(z->frames);
      free(z);
      dreturn("%i", 1);
      return 1;
    }

    /* Set persistent parameters once; they survive ZSTD_reset_session_only */
    r = ZSTD_CCtx_setParameter(z->cctx, ZSTD_c_compressionLevel,
        GD_ZSTD_COMP_LEVEL);
    if (!ZSTD_isError(r))
      r = ZSTD_CCtx_setParameter(z->cctx, ZSTD_c_stableInBuffer, 1);
    if (ZSTD_isError(r)) {
      ZSTD_freeCCtx(z->cctx);
      close(z->fd);
      free(z->frames);
      free(z);
      dreturn("%i", 1);
      return 1;
    }

    z->write_buf = malloc(frame_size);
    if (z->write_buf == NULL) {
      ZSTD_freeCCtx(z->cctx);
      close(z->fd);
      free(z->frames);
      free(z);
      dreturn("%i", 1);
      return 1;
    }

    bound = ZSTD_compressBound(frame_size);
    if (ZSTD_isError(bound) || bound > UINT32_MAX) {
      free(z->write_buf);
      ZSTD_freeCCtx(z->cctx);
      close(z->fd);
      free(z->frames);
      free(z);
      dreturn("%i", 1);
      return 1;
    }
    z->out_buf_size = (uint32_t)bound;
    z->out_buf = malloc(z->out_buf_size);
    if (z->out_buf == NULL) {
      free(z->write_buf);
      ZSTD_freeCCtx(z->cctx);
      close(z->fd);
      free(z->frames);
      free(z);
      dreturn("%i", 1);
      return 1;
    }

    /* Append after all existing frames */
    if (z->n_frames > 0) {
      struct gd_zstd_frame *last = &z->frames[z->n_frames - 1];
      z->write_offset = last->comp_offset + last->comp_size;
    } else {
      z->write_offset = 0;
    }
  }

  if (mode & GD_FILE_READ) {
    z->dctx = ZSTD_createDCtx();
    if (z->dctx == NULL) {
      if (z->cctx) ZSTD_freeCCtx(z->cctx);
      free(z->write_buf);
      free(z->out_buf);
      close(z->fd);
      free(z->frames);
      free(z);
      dreturn("%i", 1);
      return 1;
    }
  }

  file->edata = z;
  file->idata = z->fd;
  file->mode = mode;
  file->pos = 0;

  dreturn("%i", 0);
  return 0;
}

off64_t _GD_ZstdSeek(struct gd_raw_file_ *file, off64_t sample,
    gd_type_t data_type gd_unused_, unsigned int mode gd_unused_)
{
  dtrace("%p, %" PRId64 ", <unused>, <unused>", file, (int64_t)sample);

  file->pos = sample;

  dreturn("%" PRId64, (int64_t)sample);
  return sample;
}

/* Decompress frame into the read cache.  Returns 0 on success. */
static int _GD_ZstdDecompressFrame(struct gd_zstddata *z, int32_t frame_idx)
{
  struct gd_zstd_frame *f;
  char *comp_buf;
  size_t result;

  dtrace("%p, %i", z, frame_idx);

  if (frame_idx < 0 || (uint32_t)frame_idx >= z->n_frames) {
    dreturn("%i", -1);
    return -1;
  }

  if (z->dctx == NULL) {
    z->dctx = ZSTD_createDCtx();
    if (z->dctx == NULL) {
      dreturn("%i", -1);
      return -1;
    }
  }

  f = &z->frames[frame_idx];

  if (z->read_buf_size < f->decomp_size) {
    char *new_buf = realloc(z->read_buf, f->decomp_size);
    if (new_buf == NULL) {
      dreturn("%i", -1);
      return -1;
    }
    z->read_buf = new_buf;
    z->read_buf_size = f->decomp_size;
  }

  comp_buf = malloc(f->comp_size);
  if (comp_buf == NULL) {
    dreturn("%i", -1);
    return -1;
  }

  if (lseek64(z->fd, (off64_t)f->comp_offset, SEEK_SET) == -1) {
    free(comp_buf);
    dreturn("%i", -1);
    return -1;
  }

  if (read(z->fd, comp_buf, f->comp_size) != (ssize_t)f->comp_size) {
    free(comp_buf);
    dreturn("%i", -1);
    return -1;
  }

  result = ZSTD_decompressDCtx(z->dctx, z->read_buf, f->decomp_size,
      comp_buf, f->comp_size);
  free(comp_buf);

  if (ZSTD_isError(result)) {
    z->last_error = result;
    dreturn("%i", -1);
    return -1;
  }

  z->cached_frame = frame_idx;

  dreturn("%i", 0);
  return 0;
}

ssize_t _GD_ZstdRead(struct gd_raw_file_ *restrict file, void *restrict ptr,
    gd_type_t data_type, size_t nelem)
{
  struct gd_zstddata *z = (struct gd_zstddata *)file->edata;
  const size_t sample_size = GD_SIZE(data_type);
  uint64_t byte_offset = (uint64_t)file->pos * sample_size;
  size_t bytes_remaining = nelem * sample_size;
  char *out = (char *)ptr;
  size_t bytes_read = 0;

  dtrace("%p, %p, 0x%X, %" PRIuSIZE, file, ptr, data_type, nelem);

  while (bytes_remaining > 0) {
    int32_t frame_idx;
    uint64_t frame_start;
    uint32_t offset_in_frame, avail, to_copy;

    frame_idx = _GD_ZstdLocateFrame(z, byte_offset, &frame_start);
    if (frame_idx < 0)
      break;

    offset_in_frame = (uint32_t)(byte_offset - frame_start);
    avail = z->frames[frame_idx].decomp_size - offset_in_frame;
    to_copy = (uint32_t)(bytes_remaining < avail ? bytes_remaining : avail);

    if (z->cached_frame != frame_idx) {
      if (_GD_ZstdDecompressFrame(z, frame_idx)) {
        if (bytes_read > 0)
          break;
        dreturn("%i", -1);
        return -1;
      }
    }

    memcpy(out, z->read_buf + offset_in_frame, to_copy);
    out += to_copy;
    byte_offset += to_copy;
    bytes_remaining -= to_copy;
    bytes_read += to_copy;
  }

  /* Serve data from the unflushed write buffer if the read extends past
   * all committed frames */
  if (bytes_remaining > 0 && z->write_buf != NULL && z->frame_used > 0) {
    uint64_t wb_start = z->total_decomp;
    uint64_t wb_end   = wb_start + z->frame_used;

    if (byte_offset >= wb_start && byte_offset < wb_end) {
      uint32_t wb_off  = (uint32_t)(byte_offset - wb_start);
      uint32_t wb_avail = z->frame_used - wb_off;
      uint32_t to_copy  = (uint32_t)(bytes_remaining < wb_avail ?
          bytes_remaining : wb_avail);

      memcpy(out, z->write_buf + wb_off, to_copy);
      out += to_copy;
      byte_offset += to_copy;
      bytes_remaining -= to_copy;
      bytes_read += to_copy;
    }
  }

  file->pos += (off64_t)(bytes_read / sample_size);

  dreturn("%" PRIdSIZE, (ssize_t)(bytes_read / sample_size));
  return (ssize_t)(bytes_read / sample_size);
}

ssize_t _GD_ZstdWrite(struct gd_raw_file_ *restrict file,
    const void *restrict ptr, gd_type_t data_type, size_t nelem)
{
  struct gd_zstddata *z = (struct gd_zstddata *)file->edata;
  const size_t sample_size = GD_SIZE(data_type);
  const char *in = (const char *)ptr;
  size_t bytes_remaining = nelem * sample_size;
  size_t total_written = 0;
  uint64_t write_pos;
  uint64_t current_end;

  dtrace("%p, %p, 0x%X, %" PRIuSIZE, file, ptr, data_type, nelem);

  /* Compute the byte position for this write */
  write_pos = (uint64_t)file->pos * sample_size;
  current_end = z->total_decomp + z->frame_used;

  /* Backward write into committed frames -- close any open frame first,
   * then rewrite from the affected committed frame */
  if (write_pos < z->total_decomp) {
    if (z->frame_used > 0 && _GD_ZstdEndFrame(z)) {
      dreturn("%i", -1);
      return -1;
    }
    ssize_t ret = _GD_ZstdRewriteFrom(z, file, write_pos, in,
        bytes_remaining, sample_size);
    if (ret >= 0)
      file->pos += ret;
    dreturn("%" PRIdSIZE, ret);
    return ret;
  }

  /* Overwrite within the in-progress frame: commit it first so RewriteFrom
   * can find it in the frame index, then rewrite from the affected position. */
  if (write_pos < current_end) {
    if (_GD_ZstdEndFrame(z)) {
      dreturn("%i", -1);
      return -1;
    }
    ssize_t ret = _GD_ZstdRewriteFrom(z, file, write_pos, in,
        bytes_remaining, sample_size);
    if (ret >= 0)
      file->pos += ret;
    dreturn("%" PRIdSIZE, ret);
    return ret;
  }

  /* Zero-fill any gap between current end and write_pos */
  if (current_end < write_pos) {
    uint64_t gap = write_pos - current_end;

    static const char zeros[4096];
    while (gap > 0) {
      uint32_t space = z->frame_size - z->frame_used;
      uint32_t to_zero = (uint32_t)(gap < space ? gap : space);
      if (to_zero > sizeof(zeros))
        to_zero = sizeof(zeros);

      memcpy(z->write_buf + z->frame_used, zeros, to_zero);
      z->frame_used += to_zero;

      if (z->frame_used >= z->frame_size && _GD_ZstdEndFrame(z)) {
        dreturn("%i", -1);
        return -1;
      }

      gap -= to_zero;
    }
    current_end = write_pos;
  }

  /* Append new data past current end, breaking across frame boundaries */
  while (bytes_remaining > 0) {
    uint32_t space = z->frame_size - z->frame_used;
    uint32_t to_feed = (uint32_t)(bytes_remaining < space ?
        bytes_remaining : space);

    memcpy(z->write_buf + z->frame_used, in, to_feed);
    z->frame_used += to_feed;

    in += to_feed;
    bytes_remaining -= to_feed;
    total_written += to_feed;

    if (z->frame_used >= z->frame_size && _GD_ZstdEndFrame(z)) {
      dreturn("%i", -1);
      return -1;
    }
  }

  file->pos += (off64_t)(total_written / sample_size);

  dreturn("%" PRIdSIZE, (ssize_t)(total_written / sample_size));
  return (ssize_t)(total_written / sample_size);
}

int _GD_ZstdSync(struct gd_raw_file_ *file)
{
  struct gd_zstddata *z = (struct gd_zstddata *)file->edata;

  dtrace("%p", file);

  /* Flush any partial open frame */
  if (z->frame_used > 0) {
    if (_GD_ZstdEndFrame(z)) {
      dreturn("%i", 1);
      return 1;
    }
  }

  if (fsync(z->fd)) {
    dreturn("%i", 1);
    return 1;
  }

  dreturn("%i", 0);
  return 0;
}

int _GD_ZstdClose(struct gd_raw_file_ *file)
{
  struct gd_zstddata *z = (struct gd_zstddata *)file->edata;
  int ret = 0;

  dtrace("%p", file);

  if (file->mode & GD_FILE_WRITE) {
    if (z->frame_used > 0) {
      if (_GD_ZstdEndFrame(z))
        ret = 1;
    }
  }

  if (z->cctx)
    ZSTD_freeCCtx(z->cctx);
  if (z->dctx)
    ZSTD_freeDCtx(z->dctx);

  free(z->write_buf);
  free(z->out_buf);
  free(z->read_buf);
  free(z->frames);

  if (close(z->fd))
    ret = 1;

  free(z);

  file->idata = -1;
  file->edata = NULL;
  file->mode = 0;

  dreturn("%i", ret);
  return ret;
}

off64_t _GD_ZstdSize(int dirfd, struct gd_raw_file_ *file,
    gd_type_t data_type, int swap gd_unused_)
{
  struct gd_zstddata z;
  off64_t size;
  int fd;

  dtrace("%i, %p, 0x%X, <unused>", dirfd, file, data_type);

  fd = gd_OpenAt(file->D, dirfd, file->name, O_RDONLY | O_BINARY, 0666);
  if (fd < 0) {
    dreturn("%i", -1);
    return -1;
  }

  memset(&z, 0, sizeof(z));
  z.fd = fd;
  z.cached_frame = -1;

  if (_GD_ZstdScanFrames(&z)) {
    free(z.frames);
    close(fd);
    dreturn("%i", -1);
    return -1;
  }

  size = (off64_t)(z.total_decomp / GD_SIZE(data_type));

  free(z.frames);
  close(fd);

  dreturn("%" PRId64, (int64_t)size);
  return size;
}

int _GD_ZstdStrerr(const struct gd_raw_file_ *file, char *buf, size_t buflen)
{
  const struct gd_zstddata *z;
  int r = 0;

  dtrace("%p, %p, %" PRIuSIZE, file, buf, buflen);

  if (file->edata) {
    z = (const struct gd_zstddata *)file->edata;
    if (ZSTD_isError(z->last_error)) {
      const char *msg = ZSTD_getErrorName(z->last_error);
      strncpy(buf, msg, buflen);
      buf[buflen - 1] = 0;
    } else {
      r = gd_StrError(errno, buf, buflen);
    }
  } else {
    r = gd_StrError(errno, buf, buflen);
  }

  dreturn("%i", r);
  return r;
}
