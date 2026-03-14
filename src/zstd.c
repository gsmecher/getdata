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
 * standard zstd frame header (ZSTD_compressCCtx does this by default).
 *
 * Writes are in-place and append-only for forward writes.  Backward writes
 * into already-committed frames trigger a rewrite: the affected portion of the
 * file is decompressed, patched, recompressed, and written back in-place
 * (truncating the file at the rewrite point first).  On open, frame headers
 * are scanned to build an in-memory index for random-access reads.  Each read
 * decompresses only the frame(s) covering the requested byte range.
 *
 * Only frames with known content sizes are indexed.  Frames produced by
 * ZSTD_compressCCtx (single-pass compression) always record content size.
 * Externally-produced frames using streaming mode may omit it; such frames
 * are skipped during scanning.
 *
 * Note: ZSTD_getFrameHeader and ZSTD_frameHeader require
 * ZSTD_STATIC_LINKING_ONLY, which ties the build to the specific libzstd
 * version it was compiled against.
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
  uint64_t total_decomp;  /* total decompressed size in bytes */

  /* Read state */
  ZSTD_DCtx *dctx;
  char *read_buf;         /* decompressed frame cache */
  uint32_t read_buf_size;
  int32_t cached_frame;   /* which frame is cached, -1 = none */

  /* Write state */
  ZSTD_CCtx *cctx;
  char *write_buf;        /* accumulation buffer for current frame */
  uint32_t write_buf_used;
  uint32_t frame_size;    /* target uncompressed frame size */
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
      dreturn("%i", -1);
      return -1;
    }
    if (read(z->fd, hdr, hdr_read) != (ssize_t)hdr_read) {
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
      char *probe;
      size_t probe_size;

      /* If content size is unknown, fall back to reading whatever remains */
      if (zfh.frameContentSize == ZSTD_CONTENTSIZE_UNKNOWN)
        probe_size = remaining;
      else
        probe_size = ZSTD_compressBound((size_t)zfh.frameContentSize);

      if (ZSTD_isError(probe_size) || probe_size > remaining)
        probe_size = remaining;

      probe = malloc(probe_size);
      if (probe == NULL) {
        dreturn("%i", -1);
        return -1;
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
      free(probe);

      if (ZSTD_isError(comp_size))
        break;

      if (zfh.frameContentSize != ZSTD_CONTENTSIZE_UNKNOWN &&
          zfh.frameContentSize > 0)
      {
        if (comp_size > UINT32_MAX || zfh.frameContentSize > UINT32_MAX)
          break;

        if (_GD_ZstdAddFrame(z, (uint64_t)pos, (uint32_t)comp_size,
              (uint32_t)zfh.frameContentSize))
        {
          dreturn("%i", -1);
          return -1;
        }
      }

      pos += (off64_t)comp_size;
    }
  }

  dreturn("%i", 0);
  return 0;
}

/* Flush the write buffer as a compressed frame */
static int _GD_ZstdFlushFrame(struct gd_zstddata *z)
{
  size_t max_comp, comp_size;
  char *comp_buf;
  uint32_t decomp_size;

  dtrace("%p", z);

  if (z->write_buf_used == 0) {
    dreturn("%i", 0);
    return 0;
  }

  decomp_size = z->write_buf_used;
  max_comp = ZSTD_compressBound(decomp_size);
  comp_buf = malloc(max_comp);
  if (comp_buf == NULL) {
    dreturn("%i", -1);
    return -1;
  }

  comp_size = ZSTD_compressCCtx(z->cctx, comp_buf, max_comp,
      z->write_buf, decomp_size, GD_ZSTD_COMP_LEVEL);

  if (ZSTD_isError(comp_size)) {
    z->last_error = comp_size;
    free(comp_buf);
    dreturn("%i", -1);
    return -1;
  }

  if (lseek64(z->fd, (off64_t)z->write_offset, SEEK_SET) == -1) {
    free(comp_buf);
    dreturn("%i", -1);
    return -1;
  }

  if (write(z->fd, comp_buf, comp_size) != (ssize_t)comp_size) {
    free(comp_buf);
    dreturn("%i", -1);
    return -1;
  }

  if (_GD_ZstdAddFrame(z, z->write_offset, (uint32_t)comp_size, decomp_size)) {
    free(comp_buf);
    dreturn("%i", -1);
    return -1;
  }

  z->write_offset += comp_size;
  z->write_buf_used = 0;

  free(comp_buf);
  dreturn("%i", 0);
  return 0;
}

/* Find the frame index containing the given byte offset, or -1 if past EOF */
static int32_t _GD_ZstdFindFrame(const struct gd_zstddata *z,
    uint64_t byte_offset)
{
  uint64_t decomp_pos = 0;
  uint32_t i;

  for (i = 0; i < z->n_frames; ++i) {
    if (byte_offset < decomp_pos + z->frames[i].decomp_size)
      return (int32_t)i;
    decomp_pos += z->frames[i].decomp_size;
  }

  return -1;
}

/* Rewrite the file from the first affected frame onward, overlaying new data.
 * Used for backward writes into already-committed (compressed) frames.
 *
 * Decompresses all frames from the first affected one onward, merges the
 * unflushed write buffer, overlays the new data, recompresses, and writes
 * back in-place (truncating the file at the rewrite point first).
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
  first_frame = _GD_ZstdFindFrame(z, write_pos);
  if (first_frame < 0) {
    dreturn("%i", -1);
    return -1;
  }

  /* Compute decompressed byte offset where first_frame starts */
  frame_start = 0;
  for (i = 0; (int32_t)i < first_frame; ++i)
    frame_start += z->frames[i].decomp_size;

  /* Compressed byte offset of first_frame -- everything before this is kept */
  trunc_point = z->frames[first_frame].comp_offset;

  /* Total decompressed bytes from first_frame through end of committed data */
  tail_decomp_size = z->total_decomp - frame_start;

  /* Total buffer: tail frames + unflushed write buffer */
  total_size = tail_decomp_size + z->write_buf_used;

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

  /* Append unflushed write buffer contents after committed data */
  if (z->write_buf_used > 0)
    memcpy(decomp_buf + tail_decomp_size, z->write_buf, z->write_buf_used);

  /* Overlay the new write data */
  memcpy(decomp_buf + (write_pos - frame_start), data, data_len);

  /* Truncate the file at the rewrite point and recompress from there */
  if (gd_truncate(z->fd, (off64_t)trunc_point)) {
    goto error;
  }

  /* Trim the frame index back to first_frame */
  z->total_decomp = frame_start;
  z->n_frames = (uint32_t)first_frame;
  z->write_offset = trunc_point;
  z->write_buf_used = 0;

  /* Recompress the modified data as new frames via the normal flush path */
  remaining = total_size;
  recomp_off = 0;
  while (remaining > 0) {
    uint32_t chunk = (uint32_t)(remaining < z->frame_size ?
        remaining : z->frame_size);

    memcpy(z->write_buf, decomp_buf + recomp_off, chunk);
    z->write_buf_used = chunk;

    if (_GD_ZstdFlushFrame(z))
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
    z->cctx = ZSTD_createCCtx();
    if (z->cctx == NULL) {
      close(z->fd);
      free(z->frames);
      free(z);
      dreturn("%i", 1);
      return 1;
    }

    z->write_buf = malloc(z->frame_size);
    if (z->write_buf == NULL) {
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
    uint32_t offset_in_frame, avail, to_copy, i;

    frame_idx = _GD_ZstdFindFrame(z, byte_offset);
    if (frame_idx < 0)
      break;

    /* Compute decompressed start of this frame */
    frame_start = 0;
    for (i = 0; (int32_t)i < frame_idx; ++i)
      frame_start += z->frames[i].decomp_size;

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
  if (bytes_remaining > 0 && z->write_buf != NULL && z->write_buf_used > 0) {
    uint64_t wb_start = z->total_decomp;
    uint64_t wb_end = wb_start + z->write_buf_used;

    if (byte_offset >= wb_start && byte_offset < wb_end) {
      uint32_t wb_off = (uint32_t)(byte_offset - wb_start);
      uint32_t wb_avail = z->write_buf_used - wb_off;
      uint32_t to_copy = (uint32_t)(bytes_remaining < wb_avail ?
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
  current_end = z->total_decomp + z->write_buf_used;

  /* Backward write into committed frames -- rewrite from affected frame */
  if (write_pos < z->total_decomp) {
    ssize_t ret = _GD_ZstdRewriteFrom(z, file, write_pos, in,
        bytes_remaining, sample_size);
    if (ret >= 0)
      file->pos += ret;
    dreturn("%" PRIdSIZE, ret);
    return ret;
  }

  /* Handle overwrite within the unflushed write buffer.
   * write_pos >= total_decomp is guaranteed here (backward case returned above) */
  if (write_pos < current_end) {
    uint32_t buf_off = (uint32_t)(write_pos - z->total_decomp);
    uint32_t buf_avail = z->write_buf_used - buf_off;
    uint32_t to_overwrite = (uint32_t)(bytes_remaining < buf_avail ?
        bytes_remaining : buf_avail);

    memcpy(z->write_buf + buf_off, in, to_overwrite);
    in += to_overwrite;
    bytes_remaining -= to_overwrite;
    total_written += to_overwrite;
    write_pos += to_overwrite;
    current_end = z->total_decomp + z->write_buf_used;
  }

  /* Zero-fill gap between current end-of-data and the write position */
  while (current_end < write_pos) {
    uint32_t space = z->frame_size - z->write_buf_used;
    uint64_t gap = write_pos - current_end;
    uint32_t to_zero = (uint32_t)(gap < space ? gap : space);

    memset(z->write_buf + z->write_buf_used, 0, to_zero);
    z->write_buf_used += to_zero;
    current_end += to_zero;

    if (z->write_buf_used >= z->frame_size) {
      if (_GD_ZstdFlushFrame(z)) {
        dreturn("%i", -1);
        return -1;
      }
    }
  }

  /* Append new data past the current end */
  while (bytes_remaining > 0) {
    uint32_t space = z->frame_size - z->write_buf_used;
    uint32_t to_copy = (uint32_t)(bytes_remaining < space ?
        bytes_remaining : space);

    memcpy(z->write_buf + z->write_buf_used, in, to_copy);
    z->write_buf_used += to_copy;
    in += to_copy;
    bytes_remaining -= to_copy;
    total_written += to_copy;

    if (z->write_buf_used >= z->frame_size) {
      if (_GD_ZstdFlushFrame(z)) {
        if (total_written > 0)
          break;
        dreturn("%i", -1);
        return -1;
      }
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

  /* Flush any partial frame */
  if (z->write_buf_used > 0) {
    if (_GD_ZstdFlushFrame(z)) {
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
    if (z->write_buf_used > 0) {
      if (_GD_ZstdFlushFrame(z))
        ret = 1;
    }
  }

  if (z->cctx)
    ZSTD_freeCCtx(z->cctx);
  if (z->dctx)
    ZSTD_freeDCtx(z->dctx);

  free(z->write_buf);
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
