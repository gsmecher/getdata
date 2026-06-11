/* Copyright (C) 2008-2016 D. V. Wiebe
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

/* Reset all cached I/O state for a RAW entry. */
static void _GD_ResetRawIO(gd_entry_t *E)
{
  free(E->e->u.raw.file[0].name);
  E->e->u.raw.file[0].name = NULL;
  E->e->u.raw.file[0].subenc = GD_ENC_UNKNOWN;
  E->e->u.raw.file[0].mode = 0;
}

/* Remove all files created in tmp_dirfd for fieldbase, then rmdir
 * tmp_path.  Used for rollback on error. */
static void _GD_CleanupTempDir(DIRFILE *D, int tmp_dirfd,
    const char *tmp_path, const char *fieldbase)
{
  struct gd_raw_file_ probe;
  int i;

  /* Remove encoded flat file for each known subencoding */
  memset(&probe, 0, sizeof(probe));
  probe.D = D;
  probe.idata = -1;
  for (i = 0; _GD_ef[i].scheme != GD_ENC_UNSUPPORTED; i++) {
    if (_GD_MissingFramework(i, GD_EF_NAME | GD_EF_UNLINK))
      continue;
    probe.subenc = i;
    if (!(*_GD_ef[i].name)(D, NULL, &probe, fieldbase, 0, 0)) {
      gd_UnlinkAt(D, tmp_dirfd, probe.name, 0);
      free(probe.name);
      probe.name = NULL;
    }
  }

  _GD_ReleaseDir(D, tmp_dirfd);
  rmdir(tmp_path);
}

/* _GD_TransformField: stream a RAW field's data from its current layout to a
 * new layout (encoding, byte_sex, frame_offset).  Reads via the normal read
 * path and writes to a temp directory, then commits on success.
 *
 * new_fragment identifies the destination fragment (for dirfd and protection).
 * new_filebase is the destination filebase; NULL means keep the existing one.
 *
 * Returns 0 on success, -1 on failure (D->error set). */
int _GD_TransformField(DIRFILE *D, gd_entry_t *E, unsigned long new_encoding,
    unsigned long new_byte_sex, off64_t new_frame_offset,
    int new_fragment, const char *new_filebase)
{
  int frag = E->fragment_index;
  int src_dirfd = D->fragment[frag].dirfd;
  int dst_dirfd = D->fragment[new_fragment].dirfd;
  int tmp_dirfd;
  int is_index;
  int subencoding;
  int i;
  int saved_subenc;
  int oop, swap, target_open;
  off64_t eof_samples, bof, new_bof, pos, target_pos;
  size_t buf_samples, n;
  ssize_t nread;
  void *buf;
  char tmp_dirname[32];
  char *tmp_path;
  const struct encoding_t *write_enc;
  struct gd_raw_file_ target_file;
  struct gd_raw_file_ new_file;

  dtrace("%p, %p, %lu, %lu, %" PRId64 ", %i, \"%s\"",
      D, E, new_encoding, new_byte_sex, (int64_t)new_frame_offset,
      new_fragment, new_filebase);

  if (new_filebase == NULL)
    new_filebase = E->e->u.raw.filebase;

  /* Check input encoding supports read; also resolves file[0].subenc and
   * D->fragment[frag].encoding (from GD_AUTO_ENCODED to the actual scheme). */
  if (!_GD_Supports(D, E, GD_EF_NAME | GD_EF_OPEN | GD_EF_CLOSE |
        GD_EF_SEEK | GD_EF_READ | GD_EF_UNLINK))
  {
    dreturn("%i", -1);
    return -1;
  }

  /* If the caller passed the fragment's encoding as new_encoding and it was
   * GD_AUTO_ENCODED at call time, _GD_Supports has now resolved it; use the
   * resolved value. */
  if (new_encoding == GD_AUTO_ENCODED)
    new_encoding = D->fragment[frag].encoding;

  /* Find the output subencoding index by scheme.  Always scan _GD_ef rather
   * than reusing file[0].subenc: source and output subencodings are distinct
   * and may differ (e.g. when re-encoding). */
  subencoding = GD_ENC_UNKNOWN;
  for (i = 0; _GD_ef[i].scheme != GD_ENC_UNSUPPORTED; i++) {
    if (_GD_ef[i].scheme == new_encoding) {
      subencoding = i;
      break;
    }
  }

  if (subencoding == GD_ENC_UNKNOWN) {
    _GD_SetError(D, GD_E_UNKNOWN_ENCODING, GD_E_UNENC_TARGET, NULL, 0, NULL);
    dreturn("%i", -1);
    return -1;
  }

  /* Check output encoding supports write */
  if (_GD_MissingFramework(subencoding,
        GD_EF_CLOSE | GD_EF_SEEK | GD_EF_WRITE | GD_EF_SYNC))
  {
    _GD_SetError(D, GD_E_UNSUPPORTED, 0, NULL, 0, NULL);
    dreturn("%i", -1);
    return -1;
  }

  /* Check data protection */
  if (D->fragment[frag].protection & GD_PROTECT_DATA ||
      D->fragment[new_fragment].protection & GD_PROTECT_DATA)
  {
    _GD_SetError(D, GD_E_PROTECTED, GD_E_PROTECTED_DATA, NULL, 0,
        D->fragment[frag].cname);
    dreturn("%i", -1);
    return -1;
  }

  /* No-op: nothing changing */
  if (new_frame_offset == D->fragment[frag].frame_offset &&
      new_encoding == D->fragment[frag].encoding &&
      new_byte_sex == D->fragment[frag].byte_sex &&
      strcmp(new_filebase, E->e->u.raw.filebase) == 0 &&
      dst_dirfd == src_dirfd)
  {
    dreturn("%i", 0);
    return 0;
  }

  /* Close any open handles before streaming */
  _GD_FiniRawIO(D, E, frag, GD_FINIRAW_KEEP);

  /* Determine data range under current layout */
  is_index = 0;
  eof_samples = _GD_GetEOF(D, E, E->field, &is_index);
  bof = D->fragment[frag].frame_offset * E->EN(raw,spf);

  if (D->error || eof_samples <= bof) {
    /* Empty field -- no data to stream */
    D->error = GD_E_OK;
    dreturn("%i", 0);
    return 0;
  }

  /* Create a temp directory in the destination fragment's directory. */
  strcpy(tmp_dirname, "_xfrm_XXXXXX");
  if (_GD_MakeTempDir(D, dst_dirfd, tmp_dirname)) {
    _GD_SetError(D, GD_E_IO, GD_E_IO_OPEN, NULL, 0, NULL);
    dreturn("%i", -1);
    return -1;
  }

  tmp_path = _GD_MakeFullPath(D, dst_dirfd, tmp_dirname, 0);
  if (tmp_path == NULL) {
    dreturn("%i", -1);
    return -1;
  }

  /* Register the temp directory in D->dir[] and get a dirfd for it.  We
   * avoid gd_OpenAt here because on platforms where open() refuses
   * directories (e.g. MSVCRT) there's nothing to open -- the dirfd is just
   * an index into the directory cache. */
  tmp_dirfd = _GD_GrabDir(D, dst_dirfd, tmp_dirname);
  if (tmp_dirfd < 0) {
    _GD_SetError(D, GD_E_IO, GD_E_IO_OPEN, tmp_path, 0, NULL);
    rmdir(tmp_path);
    free(tmp_path);
    dreturn("%i", -1);
    return -1;
  }

  /* Allocate streaming buffer */
  buf_samples = GD_BUFFER_SIZE / E->e->u.raw.size;
  buf = _GD_Malloc(D, GD_BUFFER_SIZE);
  if (buf == NULL) {
    _GD_ReleaseDir(D, tmp_dirfd);
    rmdir(tmp_path);
    free(tmp_path);
    dreturn("%i", -1);
    return -1;
  }

  saved_subenc = E->e->u.raw.file[0].subenc;

  /* Start reading at the later of the old BOF and the new BOF.
   * Data before the new frame offset is truncated (not carried over). */
  new_bof = new_frame_offset * E->EN(raw,spf);
  if (new_bof > bof)
    bof = new_bof;

  /* Determine whether the target encoding is OOP.  OOP encodings (gzip,
   * bzip2, lzma, FLAC) can't write incrementally to compressed format, so
   * we write raw and encode as a post-pass.  Non-OOP encodings (raw, zstd,
   * text, SIE) are written directly through the target encoder. */
  oop = (_GD_ef[subencoding].flags & GD_EF_OOP) ? 1 : 0;
  write_enc = oop ? &_GD_ef[GD_ENC_RAW] : &_GD_ef[subencoding];
  swap = _GD_CheckByteSex(E->EN(raw,data_type), new_byte_sex, 0, 0, NULL);

  /* Local file handle for the write side -- completely independent of
   * E->e->u.raw.file[], which is used only by the read path. */
  memset(&target_file, 0, sizeof(target_file));
  target_file.D = D;
  target_file.subenc = oop ? GD_ENC_RAW : subencoding;
  target_file.idata = -1;
  target_open = 0;

  /* Stream: read via normal path (handles encoding resolution), write to
   * tmp_dirfd via local file handle and direct encoding calls. */
  for (pos = bof; pos < eof_samples; pos += (off64_t)nread) {
    n = (size_t)(eof_samples - pos);
    if (n > buf_samples)
      n = buf_samples;

    /* Read from source */
    nread = (ssize_t)_GD_DoField(D, E, 0, pos, n, E->EN(raw,data_type), buf);
    if (nread <= 0 || D->error)
      break;

    /* Close source so it doesn't hold the fd */
    _GD_FiniRawIO(D, E, frag, GD_FINIRAW_KEEP);

    /* Byte-swap for target layout.  ECOR encodings need the framework to
     * fix byte order; SWAP encodings (FLAC) handle it internally.  For
     * OOP targets we write raw, so always apply ECOR correction. */
    if (_GD_ef[subencoding].flags & GD_EF_ECOR)
      _GD_FixEndianness(buf, (size_t)nread, E->EN(raw,data_type), 0,
          new_byte_sex);

    /* Open target file on first write */
    if (!target_open) {
      if ((*write_enc->name)(D, NULL, &target_file, new_filebase, 0, 0) ||
          (*write_enc->open)(tmp_dirfd, &target_file, E->EN(raw,data_type),
            swap, GD_FILE_WRITE))
      {
        _GD_SetError(D, GD_E_IO, GD_E_IO_OPEN, new_filebase, 0, NULL);
        break;
      }
      target_open = 1;
    }

    /* Seek and write */
    target_pos = pos - new_frame_offset * E->EN(raw,spf);
    (*write_enc->seek)(&target_file, target_pos, E->EN(raw,data_type),
        GD_FILE_WRITE);
    if ((*write_enc->write)(&target_file, buf, E->EN(raw,data_type),
          (size_t)nread) < nread)
    {
      _GD_SetError(D, GD_E_IO, GD_E_IO_WRITE, new_filebase, 0, NULL);
      break;
    }
  }

  /* Close the write-side file */
  if (target_open)
    (*write_enc->close)(&target_file);

  free(buf);

  /* For OOP targets, encode the raw file we just wrote: read raw, write
   * through encoder, rename for OOP, delete raw. */
  if (!D->error && oop && target_open && subencoding != GD_ENC_RAW) {
    struct gd_raw_file_ enc_file;
    char *final_name;
    off64_t raw_nsamples;
    ssize_t nr, nw;
    size_t enc_buflen;
    void *enc_buf;

    /* Read back the raw file */
    raw_nsamples = (*_GD_ef[GD_ENC_RAW].size)(tmp_dirfd, &target_file,
        E->EN(raw,data_type), 0);

    if (raw_nsamples > 0) {
      enc_buflen = (size_t)raw_nsamples * E->e->u.raw.size;
      enc_buf = malloc(enc_buflen);
      if (enc_buf == NULL) {
        _GD_SetError(D, GD_E_ALLOC, 0, NULL, 0, NULL);
      } else {
        /* Reopen raw file for reading */
        target_file.idata = -1;
        if ((*_GD_ef[GD_ENC_RAW].open)(tmp_dirfd, &target_file,
              E->EN(raw,data_type), 0, GD_FILE_READ) == 0)
        {
          (*_GD_ef[GD_ENC_RAW].seek)(&target_file, 0, E->EN(raw,data_type),
              GD_FILE_READ);
          nr = (*_GD_ef[GD_ENC_RAW].read)(&target_file, enc_buf,
              E->EN(raw,data_type), (size_t)raw_nsamples);
          (*_GD_ef[GD_ENC_RAW].close)(&target_file);

          if (nr >= raw_nsamples) {
            /* Write through the target encoder */
            memset(&enc_file, 0, sizeof(enc_file));
            enc_file.D = D;
            enc_file.subenc = subencoding;
            enc_file.idata = -1;

            /* Get final encoded filename */
            if (!(*_GD_ef[subencoding].name)(D, NULL, &enc_file,
                  new_filebase, 0, 0))
            {
              final_name = enc_file.name;
              enc_file.name = NULL;

              /* OOP: open as temp, write, close, rename */
              if (!(*_GD_ef[subencoding].name)(D, NULL, &enc_file,
                    new_filebase, 1, 0) &&
                  !(*_GD_ef[subencoding].open)(tmp_dirfd, &enc_file,
                    E->EN(raw,data_type), swap,
                    GD_FILE_WRITE | GD_FILE_TEMP))
              {
                (*_GD_ef[subencoding].seek)(&enc_file, 0,
                    E->EN(raw,data_type), GD_FILE_WRITE);
                nw = (*_GD_ef[subencoding].write)(&enc_file, enc_buf,
                    E->EN(raw,data_type), (size_t)nr);
                (*_GD_ef[subencoding].close)(&enc_file);

                if (nw >= nr) {
                  gd_RenameAt(D, tmp_dirfd, enc_file.name, tmp_dirfd,
                      final_name);

                  /* Delete the raw file */
                  gd_UnlinkAt(D, tmp_dirfd, target_file.name, 0);
                } else {
                  _GD_SetError(D, GD_E_IO, GD_E_IO_WRITE, new_filebase,
                      0, NULL);
                }
              }
              free(enc_file.name);
              free(final_name);
            }
          }
        }
        free(enc_buf);
      }
    }
  }

  free(target_file.name);
  target_file.name = NULL;

  if (D->error) {
    E->e->u.raw.file[0].subenc = saved_subenc;
    _GD_CleanupTempDir(D, tmp_dirfd, tmp_path, new_filebase);
    free(tmp_path);
    dreturn("%i", -1);
    return -1;
  }

  /* Commit: delete old data */
  memset(&new_file, 0, sizeof(new_file));
  new_file.D = D;
  new_file.idata = -1;
  new_file.subenc = saved_subenc;
  if (!(*_GD_ef[saved_subenc].name)(D,
        (const char *)D->fragment[frag].enc_data, &new_file,
        E->e->u.raw.filebase, 0, 0))
  {
    (*_GD_ef[saved_subenc].unlink)(src_dirfd, &new_file);
    free(new_file.name);
  }

  /* Move new data from temp dir to destination */
  memset(&new_file, 0, sizeof(new_file));
  new_file.D = D;
  new_file.idata = -1;
  new_file.subenc = subencoding;
  if ((*_GD_ef[subencoding].name)(D, NULL, &new_file, new_filebase, 0, 0))
  {
    _GD_ReleaseDir(D, tmp_dirfd);
    free(tmp_path);
    _GD_ResetRawIO(E);
    dreturn("%i", -1);
    return -1;
  }
  if (gd_RenameAt(D, tmp_dirfd, new_file.name, dst_dirfd, new_file.name)) {
    _GD_SetError(D, GD_E_IO, GD_E_IO_RENAME, new_file.name, 0, NULL);
    free(new_file.name);
    _GD_ReleaseDir(D, tmp_dirfd);
    free(tmp_path);
    _GD_ResetRawIO(E);
    dreturn("%i", -1);
    return -1;
  }
  free(new_file.name);

  _GD_ReleaseDir(D, tmp_dirfd);
  rmdir(tmp_path);
  free(tmp_path);
  _GD_ResetRawIO(E);

  dreturn("%i", 0);
  return 0;
}


int _GD_StrCmpNull(const char *s1, const char *s2)
{
  int r;

  dtrace("%p, %p", s1, s2);

  if (s1 == NULL && s2 == NULL) {
    dreturn("%i", 0);
    return 0;
  }

  if (s1 == NULL) {
    dreturn("%i", -1);
    return -1;
  }

  if (s2 == NULL) {
    dreturn("%i", 1);
    return 1;
  }

  r = strcmp(s1, s2);

  dreturn("%i", r);
  return r;
}

static int _GD_Move(DIRFILE *D, gd_entry_t *E, int new_fragment, unsigned flags)
{
  char *new_filebase, *new_code;
  size_t new_len;
  struct gd_rename_data_ *rdat = NULL;
  int i;

  dtrace("%p, %p, %i, 0x%X", D, E, new_fragment, flags);

  if ((D->flags & GD_ACCMODE) == GD_RDONLY)
    GD_SET_RETURN_ERROR(D, GD_E_ACCMODE, 0, NULL, 0, NULL);

  /* check metadata protection */
  if (D->fragment[E->fragment_index].protection & GD_PROTECT_FORMAT ||
      D->fragment[new_fragment].protection & GD_PROTECT_FORMAT)
  {
    GD_SET_RETURN_ERROR(D, GD_E_PROTECTED, GD_E_PROTECTED_FORMAT, NULL, 0,
        D->fragment[E->fragment_index].cname);
  }

  /* Compose the field's new name */

  /* remove the old affixes */
  new_filebase = _GD_StripCode(D, E->fragment_index, E->field, GD_CO_NSALL
      | GD_CO_ASSERT);

  if (!new_filebase) /* Alloc error */
    GD_RETURN_ERROR(D);

  /* add the new affixes */
  new_code = _GD_BuildCode(D, new_fragment, NULL, 0, new_filebase,
      E->flags & GD_EN_EARLY, NULL);
  new_len = strlen(new_code);

  if (new_len != E->e->len || memcmp(new_code, E->field, new_len)) {
    /* duplicate check */
    if (_GD_FindField(D, new_code, new_len, D->entry, D->n_entries, 1, NULL)) {
      _GD_SetError(D, GD_E_DUPLICATE, 0, NULL, 0, new_code);
      free(new_filebase);
      free(new_code);
      GD_RETURN_ERROR(D);
    }

    rdat = _GD_PrepareRename(D, new_code, new_len, E, new_fragment, flags);
    if (rdat == NULL) {
      free(new_filebase);
      GD_RETURN_ERROR(D);
    }
  } else {
    free(new_code);
    new_code = NULL;
  }

  if ((flags & GD_REN_DATA) && E->field_type == GD_RAW_ENTRY &&
      (D->fragment[E->fragment_index].encoding !=
       D->fragment[new_fragment].encoding ||
       D->fragment[E->fragment_index].byte_sex !=
       D->fragment[new_fragment].byte_sex ||
       D->fragment[E->fragment_index].frame_offset !=
       D->fragment[new_fragment].frame_offset ||
       _GD_StrCmpNull(D->fragment[E->fragment_index].sname,
         D->fragment[new_fragment].sname)))
  {
    if (_GD_TransformField(D, E, D->fragment[new_fragment].encoding,
          D->fragment[new_fragment].byte_sex,
          D->fragment[new_fragment].frame_offset,
          new_fragment, new_filebase))
    {
      _GD_CleanUpRename(rdat, 1);
      GD_RETURN_ERROR(D);
    }
  } else
    free(new_filebase);

  /* nothing from now on may fail */
  D->fragment[E->fragment_index].modified = 1;
  D->fragment[new_fragment].modified = 1;
  D->flags &= ~GD_HAVE_VERSION;

  /* update metadata */
  E->fragment_index = new_fragment;
  for (i = 0; i < E->e->n_meta; ++i)
    E->e->p.meta_entry[i]->fragment_index = new_fragment;

  if (rdat)
    _GD_PerformRename(D, rdat);

  /* resort */
  if (new_code)
    qsort(D->entry, D->n_entries, sizeof(gd_entry_t*), _GD_EntryCmp);

  dreturn("%i", 0);
  return 0;
}

int gd_move(DIRFILE *D, const char *field_code, int new_fragment,
    unsigned flags)
{
  gd_entry_t *E;
  int ret;

  dtrace("%p, \"%s\", %i, 0x%X", D, field_code, new_fragment, flags);

  GD_RETURN_ERR_IF_INVALID(D);

  E = _GD_FindField(D, field_code, strlen(field_code), D->entry, D->n_entries,
      0, NULL);

  if (E == NULL)
    GD_SET_RETURN_ERROR(D, GD_E_BAD_CODE, GD_E_CODE_MISSING, NULL, 0,
        field_code);

  if (E->field_type == GD_INDEX_ENTRY)
    GD_SET_RETURN_ERROR(D, GD_E_BAD_FIELD_TYPE, GD_E_FIELD_BAD, NULL, 0,
        "INDEX");

  if (new_fragment < 0 || new_fragment >= D->n_fragment)
    GD_SET_RETURN_ERROR(D, GD_E_BAD_INDEX, 0, NULL, new_fragment, NULL);

  if (E->fragment_index == new_fragment) {
    dreturn("%i", 0);
    return 0;
  }

  ret = _GD_Move(D, E, new_fragment, flags);

  dreturn("%i", ret);
  return ret;
}
