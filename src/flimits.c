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

static void _GD_ShiftFragment(DIRFILE* D, off64_t offset, int fragment,
    int move)
{
  unsigned int i;

  dtrace("%p, %" PRId64 ", %i, %i", D, (int64_t)offset, fragment, move);

  /* check protection */
  if (D->fragment[fragment].protection & GD_PROTECT_FORMAT) {
    _GD_SetError(D, GD_E_PROTECTED, GD_E_PROTECTED_FORMAT, NULL, 0,
        D->fragment[fragment].cname);
    dreturnvoid();
    return;
  }

  if (move && offset != D->fragment[fragment].frame_offset) {
    for (i = 0; i < D->n_entries; ++i)
      if (D->entry[i]->fragment_index == fragment &&
          D->entry[i]->field_type == GD_RAW_ENTRY)
      {
        if (_GD_TransformField(D, D->entry[i],
              D->fragment[fragment].encoding,
              D->fragment[fragment].byte_sex, offset,
              D->fragment[fragment].chunk_size, fragment, NULL))
          break;
      }

    if (D->error) {
      dreturnvoid();
      return;
    }
  }

  D->fragment[fragment].frame_offset = offset;
  D->fragment[fragment].modified = 1;
  D->flags &= ~GD_HAVE_VERSION;

  dreturnvoid();
}

int gd_alter_frameoffset64(DIRFILE* D, off64_t offset, int fragment, int move)
{
  int i;

  dtrace("%p, %" PRId64 ", %i, %i", D, (int64_t)offset, fragment, move);

  GD_RETURN_ERR_IF_INVALID(D);

  if ((D->flags & GD_ACCMODE) != GD_RDWR)
    _GD_SetError(D, GD_E_ACCMODE, 0, NULL, 0, NULL);
  else if (fragment < GD_ALL_FRAGMENTS || fragment >= D->n_fragment)
    _GD_SetError(D, GD_E_BAD_INDEX, 0, NULL, 0, NULL);
  else if (offset < 0)
    _GD_SetError(D, GD_E_RANGE, GD_E_OUT_OF_RANGE, NULL, 0, NULL);
  else if (fragment == GD_ALL_FRAGMENTS) {
    for (i = 0; i < D->n_fragment; ++i) {
      _GD_ShiftFragment(D, offset, i, move);

      if (D->error)
        break;
    }
  } else
    _GD_ShiftFragment(D, offset, fragment, move);

  GD_RETURN_ERROR(D);
}

off64_t gd_frameoffset64(DIRFILE* D, int fragment)
{
  dtrace("%p, %i", D, fragment);

  GD_RETURN_ERR_IF_INVALID(D);

  if (fragment < 0 || fragment >= D->n_fragment)
    GD_SET_RETURN_ERROR(D, GD_E_BAD_INDEX, 0, NULL, 0, NULL);

  dreturn("%" PRId64, (int64_t)D->fragment[fragment].frame_offset);
  return D->fragment[fragment].frame_offset;
}

#if !(defined _FILE_OFFSET_BITS && _FILE_OFFSET_BITS == 64)
/* 32(ish)-bit wrappers for the 64-bit versions, when needed */
int gd_alter_frameoffset(DIRFILE* D, off_t offset, int fragment, int move)
{
  return gd_alter_frameoffset64(D, offset, fragment, move);
}

off_t gd_frameoffset(DIRFILE* D, int fragment) gd_nothrow
{
  return gd_frameoffset64(D, fragment);
}

#endif

/* Rechunk a single RAW field: read data from the current layout, delete old
 * files, and write data into the new layout.  The fragment's chunk_size must
 * already be set to the NEW value before calling this.  old_chunk_size is the
 * previous value.  Streams through GD_BUFFER_SIZE so memory is bounded. */
static int _GD_RechunkField(DIRFILE *D, gd_entry_t *E,
    off64_t old_chunk_size, off64_t new_chunk_size)
{
  int frag = E->fragment_index;
  off64_t bof, eof_samples, pos, tmp_pos;
  int is_index, ret;
  size_t sample_size, buf_samples, n;
  ssize_t nread, nwrote;
  char *buf, *tmp_filebase;
  struct gd_raw_file_ tmp;

  dtrace("%p, %p, %" PRId64 ", %" PRId64, D, E,
      (int64_t)old_chunk_size, (int64_t)new_chunk_size);

  /* No change */
  if (old_chunk_size == new_chunk_size) {
    dreturn("%i", 0);
    return 0;
  }

  /* Find data range.  Temporarily restore old chunk_size for reading. */
  D->fragment[frag].chunk_size = old_chunk_size;

  bof = D->fragment[frag].frame_offset * E->EN(raw,spf);
  is_index = 0;
  eof_samples = _GD_GetEOF(D, E, E->field, &is_index);
  if (D->error || eof_samples <= bof) {
    /* Empty field or error -- just clean up old files */
    if (old_chunk_size > 0)
      _GD_ChunkUnlink(D, E);
    else {
      /* Remove non-chunked data file via encoding plugin */
      if (_GD_Supports(D, E, GD_EF_NAME | GD_EF_UNLINK))
        (*_GD_ef[E->e->u.raw.file[0].subenc].unlink)(
            D->fragment[frag].dirfd, E->e->u.raw.file);
    }
    D->fragment[frag].chunk_size = new_chunk_size;
    D->error = GD_E_OK;

    /* Reset cached state for new layout */
    free(E->e->u.raw.file[0].name);
    E->e->u.raw.file[0].name = NULL;
    E->e->u.raw.file[0].subenc = GD_ENC_UNKNOWN;
    E->e->u.raw.file[0].mode = 0;
    E->e->u.raw.active_chunk = -1;
    E->e->u.raw.first_chunk = -1;
    E->e->u.raw.last_chunk = -1;
    E->e->u.raw.cursor_is_raw = 0;

    dreturn("%i", 0);
    return 0;
  }

  /* Read all data into the framework's read path (chunk-aware) */
  sample_size = GD_SIZE(E->EN(raw,data_type));
  buf_samples = GD_BUFFER_SIZE / sample_size;
  buf = _GD_Malloc(D, GD_BUFFER_SIZE);
  if (buf == NULL) {
    D->fragment[frag].chunk_size = new_chunk_size;
    dreturn("%i", -1);
    return -1;
  }

  /* Phase 1: Read all data and store in a temp file.  We use a flat raw
   * temp file to avoid needing chunk-aware writes during the copy. */
  memset(&tmp, 0, sizeof(tmp));
  tmp.D = D;
  tmp.subenc = GD_ENC_RAW;
  tmp.idata = -1;

  tmp_filebase = _GD_Malloc(D,
      strlen(E->e->u.raw.filebase) + 12);
  if (tmp_filebase == NULL) {
    free(buf);
    D->fragment[frag].chunk_size = new_chunk_size;
    dreturn("%i", -1);
    return -1;
  }
  sprintf(tmp_filebase, "%s_rechunk", E->e->u.raw.filebase);

  if ((*_GD_ef[GD_ENC_RAW].name)(D, NULL, &tmp, tmp_filebase, 0, 0)) {
    free(tmp_filebase);
    free(buf);
    D->fragment[frag].chunk_size = new_chunk_size;
    dreturn("%i", -1);
    return -1;
  }

  if ((*_GD_ef[GD_ENC_RAW].open)(D->fragment[frag].dirfd, &tmp,
        E->EN(raw,data_type), 0, GD_FILE_WRITE))
  {
    free(tmp.name);
    free(tmp_filebase);
    free(buf);
    D->fragment[frag].chunk_size = new_chunk_size;
    dreturn("%i", -1);
    return -1;
  }

  /* Read from old layout via the normal read path */
  tmp_pos = 0;
  for (pos = bof; pos < eof_samples; pos += (off64_t)n) {
    n = (size_t)(eof_samples - pos);
    if (n > buf_samples) n = buf_samples;

    nread = (ssize_t)gd_getdata(D, E->field, pos / E->EN(raw,spf),
        pos % E->EN(raw,spf), 0, n, E->EN(raw,data_type), buf);
    if (nread <= 0) break;

    (*_GD_ef[GD_ENC_RAW].seek)(&tmp, tmp_pos, E->EN(raw,data_type),
        GD_FILE_WRITE);
    nwrote = (*_GD_ef[GD_ENC_RAW].write)(&tmp, buf, E->EN(raw,data_type),
        (size_t)nread);
    if (nwrote < nread) break;
    tmp_pos += nwrote;
  }

  (*_GD_ef[GD_ENC_RAW].close)(&tmp);

  /* Phase 2: Delete old layout */
  _GD_Flush(D, E, 0, 1);
  if (old_chunk_size > 0)
    _GD_ChunkUnlink(D, E);
  else if (_GD_Supports(D, E, GD_EF_NAME | GD_EF_UNLINK))
    (*_GD_ef[E->e->u.raw.file[0].subenc].unlink)(
        D->fragment[frag].dirfd, E->e->u.raw.file);

  /* Phase 3: Set new chunk_size and write data from temp file */
  D->fragment[frag].chunk_size = new_chunk_size;

  /* Reset entry state for new layout */
  free(E->e->u.raw.file[0].name);
  E->e->u.raw.file[0].name = NULL;
  E->e->u.raw.file[0].subenc = GD_ENC_UNKNOWN;
  E->e->u.raw.active_chunk = -1;
  E->e->u.raw.first_chunk = -1;
  E->e->u.raw.last_chunk = -1;
  E->e->u.raw.cursor_is_raw = 0;

  /* Reopen temp file for reading */
  tmp.idata = -1;
  if ((*_GD_ef[GD_ENC_RAW].open)(D->fragment[frag].dirfd, &tmp,
        E->EN(raw,data_type), 0, GD_FILE_READ) == 0)
  {
    /* Write through the normal write path (chunk-aware) */
    (*_GD_ef[GD_ENC_RAW].seek)(&tmp, 0, E->EN(raw,data_type),
        GD_FILE_READ);
    pos = bof;
    for (;;) {
      nread = (*_GD_ef[GD_ENC_RAW].read)(&tmp, buf,
          E->EN(raw,data_type), buf_samples);
      if (nread <= 0) break;

      nwrote = (ssize_t)gd_putdata(D, E->field, pos / E->EN(raw,spf),
          pos % E->EN(raw,spf), 0, (size_t)nread,
          E->EN(raw,data_type), buf);
      if (nwrote < nread) break;
      pos += nwrote;
    }

    (*_GD_ef[GD_ENC_RAW].close)(&tmp);
  }

  /* Clean up: close new files and delete temp */
  _GD_Flush(D, E, 0, 1);
  gd_UnlinkAt(D, D->fragment[frag].dirfd, tmp.name, 0);
  free(tmp.name);
  free(tmp_filebase);

  free(buf);

  /* Final state reset */
  free(E->e->u.raw.file[0].name);
  E->e->u.raw.file[0].name = NULL;
  E->e->u.raw.file[0].subenc = GD_ENC_UNKNOWN;
  E->e->u.raw.file[0].mode = 0;
  E->e->u.raw.active_chunk = -1;
  E->e->u.raw.first_chunk = -1;
  E->e->u.raw.last_chunk = -1;
  E->e->u.raw.cursor_is_raw = 0;

  ret = D->error ? -1 : 0;
  dreturn("%i", ret);
  return ret;
}

/* Set the chunk size for a fragment, re-chunking existing data as needed.
 * Streams data through a bounded buffer so memory usage is O(GD_BUFFER_SIZE)
 * regardless of field size. */
int gd_alter_chunk_size64(DIRFILE* D, off64_t chunk_size, int fragment)
{
  int i, first, last;

  dtrace("%p, %" PRId64 ", %i", D, (int64_t)chunk_size, fragment);

  GD_RETURN_ERR_IF_INVALID(D);

  if ((D->flags & GD_ACCMODE) != GD_RDWR)
    GD_SET_RETURN_ERROR(D, GD_E_ACCMODE, 0, NULL, 0, NULL);
  if (fragment < GD_ALL_FRAGMENTS || fragment >= D->n_fragment)
    GD_SET_RETURN_ERROR(D, GD_E_BAD_INDEX, 0, NULL, 0, NULL);
  if (chunk_size < 0)
    GD_SET_RETURN_ERROR(D, GD_E_RANGE, GD_E_OUT_OF_RANGE, NULL, 0, NULL);

  first = (fragment == GD_ALL_FRAGMENTS) ? 0 : fragment;
  last = (fragment == GD_ALL_FRAGMENTS) ? D->n_fragment : fragment + 1;

  for (i = 0; i < (int)D->n_entries; ++i) {
    gd_entry_t *E = D->entry[i];
    off64_t old_cs;

    if (E->field_type != GD_RAW_ENTRY)
      continue;
    if (E->fragment_index < first || E->fragment_index >= last)
      continue;

    old_cs = D->fragment[E->fragment_index].chunk_size;
    if (_GD_RechunkField(D, E, old_cs, chunk_size))
      GD_RETURN_ERROR(D);
  }

  for (i = first; i < last; ++i) {
    D->fragment[i].chunk_size = chunk_size;
    D->fragment[i].modified = 1;
  }

  dreturn("%i", 0);
  return 0;
}

#if !(defined _FILE_OFFSET_BITS && _FILE_OFFSET_BITS == 64)
int gd_alter_chunk_size(DIRFILE* D, off_t chunk_size, int fragment)
{
  return gd_alter_chunk_size64(D, chunk_size, fragment);
}
#endif

off64_t gd_chunk_size64(DIRFILE* D, int fragment)
{
  dtrace("%p, %i", D, fragment);

  GD_RETURN_ERR_IF_INVALID(D);

  if (fragment < 0 || fragment >= D->n_fragment)
    GD_SET_RETURN_ERROR(D, GD_E_BAD_INDEX, 0, NULL, 0, NULL);

  dreturn("%" PRId64, (int64_t)D->fragment[fragment].chunk_size);
  return D->fragment[fragment].chunk_size;
}

#if !(defined _FILE_OFFSET_BITS && _FILE_OFFSET_BITS == 64)
off_t gd_chunk_size(DIRFILE* D, int fragment) gd_nothrow
{
  return (off_t)gd_chunk_size64(D, fragment);
}
#endif

off64_t _GD_GetEOF(DIRFILE *restrict D, gd_entry_t *restrict E,
    const char *restrict parent, int *restrict is_index)
{
  off64_t ns = -1, ns1;
  unsigned int spf0, spf1;
  int i, is_index1;

  dtrace("%p, %p, \"%s\", %p", D, E, parent, is_index);

  if (++D->recurse_level >= GD_MAX_RECURSE_LEVEL) {
    D->recurse_level--;
    GD_SET_RETURN_ERROR(D, GD_E_RECURSE_LEVEL, GD_E_RECURSE_CODE, NULL, 0,
        E->field);
  }

  if (_GD_FindInputs(D, E, 1)) {
    D->recurse_level--;
    dreturn("%i", D->error);
    return D->error;
  }

  *is_index = 0;
  switch (E->field_type) {
    case GD_RAW_ENTRY:
      if (!_GD_Supports(D, E, GD_EF_NAME | GD_EF_SIZE))
        break;

      if (D->fragment[E->fragment_index].chunk_size > 0) {
        ns = _GD_ChunkedSampleCount(D, E);
        if (ns >= 0)
          ns += D->fragment[E->fragment_index].frame_offset * E->EN(raw,spf);
      } else {
        /* Non-chunked: original single-file path */
        if ((*_GD_ef[E->e->u.raw.file[0].subenc].name)(D,
              (const char*)D->fragment[E->fragment_index].enc_data,
              E->e->u.raw.file, E->e->u.raw.filebase, 0, 0))
        {
          break;
        }

        ns = (*_GD_ef[E->e->u.raw.file[0].subenc].size)(
            D->fragment[E->fragment_index].dirfd, E->e->u.raw.file,
            E->EN(raw,data_type), _GD_FileSwapBytes(D, E));

        if (ns < 0) {
          /* A missing file means an empty field, not an error */
          if (errno == ENOENT)
            ns = 0;
          else {
            _GD_SetEncIOError(D, GD_E_IO_READ, E->e->u.raw.file + 0);
            ns = -1;
            break;
          }
        }

        ns += D->fragment[E->fragment_index].frame_offset * E->EN(raw,spf);
      }
      break;
    case GD_BIT_ENTRY:
    case GD_LINTERP_ENTRY:
    case GD_SBIT_ENTRY:
    case GD_POLYNOM_ENTRY:
    case GD_RECIP_ENTRY:
    case GD_INDIR_ENTRY:
    case GD_SINDIR_ENTRY:
      ns = _GD_GetEOF(D, E->e->entry[0], E->field, is_index);
      break;
    case GD_DIVIDE_ENTRY:
    case GD_MULTIPLY_ENTRY:
    case GD_WINDOW_ENTRY:
    case GD_MPLEX_ENTRY:
      ns = _GD_GetEOF(D, E->e->entry[0], E->field, is_index);

      if (D->error)
        break;

      spf0 = _GD_GetSPF(D, E->e->entry[0]);

      if (D->error) {
        ns = -1;
        break;
      }

      ns1 = _GD_GetEOF(D, E->e->entry[1], E->field, &is_index1);

      if (D->error) {
        ns = -1;
        break;
      }

      if (!is_index1) {
        spf1 = _GD_GetSPF(D, E->e->entry[1]);

        if (D->error) {
          ns = -1;
          break;
        }

        ns1 = ns1 * spf0 / spf1;
        if (*is_index || ns1 < ns) {
          *is_index = is_index1;
          ns = ns1;
        }
      }
      break;
    case GD_LINCOM_ENTRY:
      ns = _GD_GetEOF(D, E->e->entry[0], E->field, is_index);

      if (D->error) {
        ns = -1;
        break;
      }

      if (E->EN(lincom,n_fields) == 1)
        break;

      spf0 = _GD_GetSPF(D, E->e->entry[0]);

      if (D->error) {
        ns = -1;
        break;
      }

      for (i = 1; i < E->EN(lincom,n_fields); ++i) {
        ns1 = _GD_GetEOF(D, E->e->entry[i], E->field, &is_index1);

        if (D->error) {
          ns = -1;
          break;
        }

        if (!is_index1) {
          spf1 = _GD_GetSPF(D, E->e->entry[i]);

          if (D->error) {
            ns = -1;
            break;
          }

          ns1 = ns1 * spf0 / spf1;
          if (*is_index || ns1 < ns) {
            *is_index = is_index1;
            ns = ns1;
          }
        }
      }
      break;
    case GD_PHASE_ENTRY:
      ns = _GD_GetEOF(D, E->e->entry[0], E->field, is_index);
      if (!*is_index && !D->error)
        ns -= E->EN(phase,shift);

      /* The EOF may never be negative. */
      if (ns < 0)
        ns = 0;

      break;
    case GD_INDEX_ENTRY:
      *is_index = 1;
      break;
    case GD_CONST_ENTRY:
    case GD_CARRAY_ENTRY:
    case GD_SARRAY_ENTRY:
    case GD_STRING_ENTRY:
      if (parent)
        _GD_SetError(D, GD_E_DIMENSION, GD_E_DIM_FORMAT, parent, 0, E->field);
      else
        _GD_SetError(D, GD_E_DIMENSION, GD_E_DIM_CALLER, NULL, 0, E->field);
      break;
    case GD_NO_ENTRY:
      _GD_SetError(D, GD_E_BAD_FIELD_TYPE, GD_E_FIELD_BAD, NULL, 0, E->field);
      break;
    case GD_ALIAS_ENTRY:
      _GD_InternalError(D);
      break;
  }

  D->recurse_level--;

  if (D->error) 
    GD_RETURN_ERROR(D);

  dreturn("%" PRId64 " %i", (int64_t)ns, *is_index);
  return ns;
}

off64_t gd_eof64(DIRFILE* D, const char *field_code)
{
  off64_t ns;
  gd_entry_t *entry;
  int is_index;

  dtrace("%p, \"%s\"", D, field_code);

  GD_RETURN_ERR_IF_INVALID(D);

  entry = _GD_FindEntry(D, field_code);

  if (D->error)
    GD_RETURN_ERROR(D);

  ns = _GD_GetEOF(D, entry, NULL, &is_index);

  if (!D->error && is_index)
    GD_SET_RETURN_ERROR(D, GD_E_BAD_FIELD_TYPE, GD_E_FIELD_BAD, NULL, 0,
        field_code);

  dreturn("%" PRId64, (int64_t)ns);
  return ns;
}

#if !(defined _FILE_OFFSET_BITS && _FILE_OFFSET_BITS == 64)
/* 32(ish)-bit wrapper for the 64-bit version, when needed */
off_t gd_eof(DIRFILE* D, const char *field_code)
{
  return (off_t)gd_eof64(D, field_code);
}
#endif

static off64_t _GD_GetBOF(DIRFILE *restrict D, gd_entry_t *restrict E,
    const char *restrict parent, unsigned int *restrict spf,
    int64_t *restrict ds)
{
  off64_t bof = -1, bof1;
  unsigned int spf1;
  int64_t ds1;
  int i;

  dtrace("%p, %p, \"%s\", %p, %p", D, E, parent, spf, ds);

  if (++D->recurse_level >= GD_MAX_RECURSE_LEVEL) {
    D->recurse_level--;
    GD_SET_RETURN_ERROR(D, GD_E_RECURSE_LEVEL, GD_E_RECURSE_CODE, NULL, 0,
        E->field);
  }

  if (_GD_FindInputs(D, E, 1)) {
    D->recurse_level--;
    dreturn("%i", D->error);
    return D->error;
  }

  switch (E->field_type) {
    case GD_RAW_ENTRY:
      bof = D->fragment[E->fragment_index].frame_offset;
      *spf = E->EN(raw,spf);
      *ds = 0;

      /* For chunked fields, find the first existing chunk */
      if (D->fragment[E->fragment_index].chunk_size > 0) {
        /* Populate cache if needed */
        if (E->e->u.raw.first_chunk < 0)
          _GD_PopulateChunkCache(D, E);

        if (E->e->u.raw.first_chunk >= 0) {
          /* Verify the cached first chunk still exists */
          if (_GD_ProbeChunk(D, E, E->e->u.raw.first_chunk, NULL) <= 0) {
            /* First chunk was deleted — rescan */
            _GD_PopulateChunkCache(D, E);
          }
          if (E->e->u.raw.first_chunk >= 0)
            bof += E->e->u.raw.first_chunk;
        }
      }
      break;
    case GD_BIT_ENTRY:
    case GD_SBIT_ENTRY:
    case GD_LINTERP_ENTRY:
    case GD_POLYNOM_ENTRY:
    case GD_RECIP_ENTRY:
    case GD_INDIR_ENTRY:
    case GD_SINDIR_ENTRY:
      bof = _GD_GetBOF(D, E->e->entry[0], E->field, spf, ds);
      break;
    case GD_PHASE_ENTRY:
      bof = _GD_GetBOF(D, E->e->entry[0], E->field, spf, ds);

      if (!D->error) {
        *ds -= E->EN(phase,shift);

        /* remove whole frames from delta-samples */
        while (*ds < 0) {
          *ds += *spf;
          bof--;
        }

        while (*ds >= *spf) {
          *ds -= *spf;
          bof++;
        }

        /* The beginning-of-frame may not be before frame zero */
        if (bof < 0)
          bof = *ds = 0;
      }

      break;
    case GD_MULTIPLY_ENTRY:
    case GD_DIVIDE_ENTRY:
    case GD_WINDOW_ENTRY:
    case GD_MPLEX_ENTRY:
      bof = _GD_GetBOF(D, E->e->entry[0], E->field, spf, ds);

      if (D->error) {
        bof = -1;
        break;
      }

      bof1 = _GD_GetBOF(D, E->e->entry[1], E->field, &spf1, &ds1);

      if (D->error) {
        bof = -1;
        break;
      }

      if (bof1 > bof ||
          (bof1 == bof && (double)ds1 / spf1 > (double)*ds / *spf))
      {
        bof = bof1;
        *ds = ds1 * *spf / spf1;
      }
      break;
    case GD_LINCOM_ENTRY:
      bof = _GD_GetBOF(D, E->e->entry[0], E->field, spf, ds);

      if (D->error) {
        bof = -1;
        break;
      }

      for (i = 1; i < E->EN(lincom,n_fields); ++i) {
        bof1 = _GD_GetBOF(D, E->e->entry[i], E->field, &spf1, &ds1);

        if (D->error) {
          bof = -1;
          break;
        }

        if (bof1 > bof ||
            (bof1 == bof && (double)ds1 / spf1 > (double)*ds / *spf))
        {
          bof = bof1;
          *ds = ds1 * *spf / spf1;
        }
      }
      break;
    case GD_INDEX_ENTRY:
      bof = 0;
      *spf = 1;
      *ds = 0;
      break;
    case GD_CONST_ENTRY:
    case GD_CARRAY_ENTRY:
    case GD_SARRAY_ENTRY:
    case GD_STRING_ENTRY:
      if (parent)
        _GD_SetError(D, GD_E_DIMENSION, GD_E_DIM_FORMAT, parent, 0, E->field);
      else
        _GD_SetError(D, GD_E_DIMENSION, GD_E_DIM_CALLER, NULL, 0, E->field);
      break;
    case GD_NO_ENTRY:
    case GD_ALIAS_ENTRY:
      _GD_InternalError(D);
      break;
  }

  D->recurse_level--;

  if (D->error)
    GD_RETURN_ERROR(D);

  dreturn("%" PRIu64 " %u %" PRId64, bof, *spf, *ds);
  return bof;
}

off64_t gd_bof64(DIRFILE* D, const char *field_code) gd_nothrow
{
  off64_t bof;
  gd_entry_t *entry;
  unsigned int spf;
  int64_t ds;

  dtrace("%p, \"%s\"", D, field_code);

  GD_RETURN_ERR_IF_INVALID(D);

  entry = _GD_FindEntry(D, field_code);

  if (D->error)
    GD_RETURN_ERROR(D);

  bof = _GD_GetBOF(D, entry, NULL, &spf, &ds);

  if (bof >= 0) /* i.e. not an error code */
    bof = bof * spf + ds;

  dreturn("%" PRId64, (int64_t)bof);
  return bof;
}

#if !(defined _FILE_OFFSET_BITS && _FILE_OFFSET_BITS == 64)
/* 32(ish)-bit wrapper for the 64-bit version, when needed */
off_t gd_bof(DIRFILE* D, const char *field_code) gd_nothrow
{
  return (off_t)gd_bof64(D, field_code);
}
#endif
/* vim: ts=2 sw=2 et tw=80
*/
