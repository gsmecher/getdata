/* (C) 2003-2005 C. Barth Netterfield
 * (C) 2003-2005 Theodore Kisner
 * (C) 2005-2008 D. V. Wiebe
 *
 ***************************************************************************
 *
 * This file is part of the GetData project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GetData is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with GetData; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "internal.h"

#ifdef STDC_HEADERS
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#endif

static size_t _GD_DoFieldOut(DIRFILE* D, const char *field_code,
    off64_t first_frame, off64_t first_samp, size_t num_frames, size_t num_samp,
    gd_type_t data_type, const void *data_in);

static size_t _GD_DoRawOut(DIRFILE *D, gd_entry_t *R,
    off64_t first_frame, off64_t first_samp, size_t num_frames, size_t num_samp,
    gd_type_t data_type, const void *data_in)
{
  off64_t s0;
  size_t ns, n_wrote;
  char datafilename[FILENAME_MAX];
  void *databuffer;

  dtrace("%p, %p, %lli, %lli, %zi, %zi, 0x%x, %p", D, R, first_frame,
      first_samp, num_frames, num_samp, data_type, data_in);

  s0 = first_samp + first_frame * R->spf;
  ns = num_samp + num_frames * R->spf;

  if (s0 < 0) {
    _GD_SetError(D, GD_E_RANGE, 0, NULL, 0, NULL);
    dreturn("%zi", 0);
    return 0;
  }

  databuffer = _GD_Alloc(D, R->data_type, ns);

  _GD_ConvertType(D, data_in, data_type, databuffer, R->data_type, ns);

  if (D->error) { /* bad input type */
    free(databuffer);
    dreturn("%zi", 0);
    return 0;
  }

  if (D->flags &
#ifdef WORDS_BIGENDIAN
      GD_LITTLE_ENDIAN
#else
      GD_BIG_ENDIAN
#endif
     )
    _GD_FixEndianness(databuffer, R->size, ns);

  /* write data to file.  Note that if the first sample is beyond     */
  /* the current end of file, a gap will result (see lseek(2)) */

  sprintf(datafilename, "%s/%s", D->name, R->e->file);

  /* Figure out the dirfile encoding type, if required */
  if ((D->flags & GD_ENCODING) == GD_AUTO_ENCODED)
    D->flags = (D->flags & ~GD_ENCODING) |
      _GD_ResolveEncoding(datafilename, 0, R->e);

  /* If the encoding is still unknown, none of the candidate files exist;
   * as a result, we don't know the intended encoding type */
  if ((D->flags & GD_ENCODING) == GD_AUTO_ENCODED) {
    _GD_SetError(D, GD_E_UNSUPPORTED, 0, NULL, 0, NULL);
    dreturn("%zi", 0);
    return 0;
  }

  /* Figure out the encoding subtype, if required */
  if (R->e->encoding == GD_ENC_UNKNOWN)
    _GD_ResolveEncoding(datafilename, D->flags & GD_ENCODING, R->e);

  if (R->e->fp < 0) {
    /* open file for reading / writing if not already opened */

    if (encode[R->e->encoding].open == NULL) {
      _GD_SetError(D, GD_E_UNSUPPORTED, 0, NULL, 0, NULL);
      dreturn("%zi", 0);
      return 0;
    } else if ((*encode[R->e->encoding].open)(R->e, datafilename,
          D->flags & GD_ACCMODE, 1))
    {
      _GD_SetError(D, GD_E_RAW_IO, 0, datafilename, errno, NULL);
      dreturn("%zi", 0);
      return 0;
    }
  }

  if (encode[R->e->encoding].seek == NULL) {
    _GD_SetError(D, GD_E_UNSUPPORTED, 0, NULL, 0, NULL);
    dreturn("%zi", 0);
    return 0;
  }

  (*encode[R->e->encoding].seek)(R->e, s0, R->data_type, 1);

  if (encode[R->e->encoding].write == NULL) {
    _GD_SetError(D, GD_E_UNSUPPORTED, 0, NULL, 0, NULL);
    dreturn("%zi", 0);
    return 0;
  }

  n_wrote = (*encode[R->e->encoding].write)(R->e, databuffer, R->data_type, ns);
  n_wrote /= R->size;

  free(databuffer);

  dreturn("%zi", n_wrote);
  return n_wrote;
}

static size_t _GD_DoLinterpOut(DIRFILE* D, gd_entry_t *I,
    off64_t first_frame, off64_t first_samp, size_t num_frames, size_t num_samp,
    gd_type_t data_type, const void *data_in)
{
  int spf;
  size_t ns;
  size_t n_wrote;

  dtrace("%p, %p, %lli, %lli, %zi, %zi, 0x%x, %p", D, I, first_frame,
      first_samp, num_frames, num_samp, data_type, data_in);

  if (I->e->table_len < 0) {
    _GD_ReadLinterpFile(D, I);
    if (D->error != GD_E_OK) {
      dreturn("%zi", 0);
      return 0;
    }
  }

  /* Interpolate X(y) instead of Y(x) */

  spf = _GD_GetSPF(I->in_fields[0], D);
  ns = num_samp + num_frames * (int)spf;

  _GD_LinterpData(D, data_in, data_type, ns, I->e->y, I->e->x, I->e->table_len);

  if (D->error != GD_E_OK) {
    dreturn("%zi", 0);
    return 0;
  }

  n_wrote = _GD_DoFieldOut(D, I->in_fields[0], first_frame, first_samp,
      num_frames, num_samp, data_type, data_in);

  dreturn("%zi", n_wrote);
  return n_wrote;
}

static size_t _GD_DoLincomOut(DIRFILE* D, gd_entry_t *L,
    off64_t first_frame, off64_t first_samp, size_t num_frames, size_t num_samp,
    gd_type_t data_type, const void *data_in)
{
  int spf;
  size_t ns, n_wrote;
  void* tmpbuf;

  dtrace("%p, %p, %lli, %lli, %zi, %zi, 0x%x, %p", D, L, first_frame,
      first_samp, num_frames, num_samp, data_type, data_in);

  /* we cannot write to LINCOM fields that are a linear combination */
  /* of more than one raw field (no way to know how to split data). */

  if (L->n_fields > 1) {
    _GD_SetError(D, GD_E_BAD_PUT_FIELD, 0, NULL, 0, L->field);
    dreturn("%zi", 0);
    return 0;
  }

  /* do the inverse scaling */
  spf = _GD_GetSPF(L->in_fields[0], D);
  ns = num_samp + num_frames * (int)spf;

  if (D->error != GD_E_OK) {
    dreturn("%zi", 0);
    return 0;
  }

  /* writeable copy */
  tmpbuf = _GD_Alloc(D, data_type, ns);

  if (tmpbuf == NULL) {
    dreturn("%zi", 0);
    return 0;
  }

  memcpy(tmpbuf, data_in, ns * GD_SIZE(data_type));

  _GD_ScaleData(D, tmpbuf, data_type, ns, 1 / L->m[0], -L->b[0] / L->m[0]);

  if (D->error != GD_E_OK) {
    free(tmpbuf);
    dreturn("%zi", 0);
    return 0;
  }

  n_wrote = _GD_DoFieldOut(D, L->in_fields[0], first_frame, first_samp,
      num_frames, num_samp, data_type, tmpbuf);
  free(tmpbuf);

  dreturn("%zi", n_wrote);
  return n_wrote;
}

static size_t _GD_DoBitOut(DIRFILE* D, gd_entry_t *B,
    off64_t first_frame, off64_t first_samp, size_t num_frames, size_t num_samp,
    gd_type_t data_type, const void *data_in)
{
  uint64_t *tmpbuf;
  uint64_t *readbuf;
  size_t i, n_wrote;
  int spf;
  size_t ns;

  dtrace("%p, %p, %lli, %lli, %zi, %zi, 0x%x, %p", D, B, first_frame,
      first_samp, num_frames, num_samp, data_type, data_in);

  const uint64_t mask = (B->numbits == 64) ? 0xffffffffffffffffULL :
    ((uint64_t)1 << B->numbits) - 1;

#ifdef GETDATA_DEBUG
  fprintf(stdout,"DoBitOut:  bitnum = %d numbits = %d mask = %llx\n",
      B->bitnum, B->numbits, mask);
#endif

  spf = _GD_GetSPF(B->in_fields[0], D);

  if (D->error != GD_E_OK) {
    dreturn("%zi", 0);
    return 0;
  }

  ns = num_samp + num_frames * (int)spf;

  tmpbuf = _GD_Alloc(D, GD_UINT64, ns);
  readbuf = _GD_Alloc(D, GD_UINT64, ns);

  if (tmpbuf == NULL || readbuf == NULL) {
    dreturn("%zi", 0);
    return 0;
  }

  memset(tmpbuf, 0, sizeof(uint64_t) * ns);
  memset(readbuf, 0, sizeof(uint64_t) * ns);

  _GD_ConvertType(D, data_in, data_type, (void*)tmpbuf, GD_UINT64, ns);

  /* first, READ the field in so that we can change the bits    */
  /* do not check error code, since the field may not exist yet */

#ifdef GETDATA_DEBUG
  fprintf(stdout,"DoBitOut:  reading in bitfield %s\n",B->in_fields[0]);
#endif

  _GD_DoField(D, B->in_fields[0], first_frame, first_samp, 0, ns, GD_UINT64,
      readbuf);

  /* error encountered, abort */
  if (D->error != GD_E_OK) {
    dreturn("%zi", 0);
    return 0;
  }

  /* now go through and set the correct bits in each field value */
  for (i = 0; i < ns; i++)
    readbuf[i] = (readbuf[i] & ~(mask << B->bitnum)) |
      (tmpbuf[i] & mask) << B->bitnum;

  /* write the modified data out */
  n_wrote = _GD_DoFieldOut(D, B->in_fields[0], first_frame, first_samp,
      num_frames, num_samp, GD_UINT64, (void*)readbuf);

  free(readbuf);
  free(tmpbuf);

  dreturn("%zi", n_wrote);
  return n_wrote;
}

static size_t _GD_DoPhaseOut(DIRFILE* D, gd_entry_t *P,
    off64_t first_frame, off64_t first_samp, size_t num_frames, size_t num_samp,
    gd_type_t data_type, const void *data_in)
{
  size_t n_wrote;

  dtrace("%p, %p, %lli, %lli, %zi, %zi, 0x%x, %p", D, P, first_frame,
      first_samp, num_frames, num_samp, data_type, data_in);

  n_wrote = _GD_DoFieldOut(D, P->in_fields[0], first_frame, first_samp +
      P->shift, num_frames, num_samp, data_type, data_in);

  dreturn("%zi", n_wrote);

  return n_wrote;
}

static size_t _GD_DoConstOut(DIRFILE* D, gd_entry_t *C, gd_type_t data_type,
    const void *data_in)
{
  dtrace("%p, %p, 0x%x, %p", D, C, data_type, data_in);

  if (C->const_type & GD_SIGNED)
    _GD_ConvertType(D, data_in, data_type, &C->e->iconst, GD_INT64, 1);
  else if (C->const_type & GD_IEEE754)
    _GD_ConvertType(D, data_in, data_type, &C->e->dconst, GD_FLOAT64, 1);
  else
    _GD_ConvertType(D, data_in, data_type, &C->e->uconst, GD_UINT64, 1);

  if (D->error) { /* bad input type */
    dreturn("%zi", 0);
    return 0;
  }

  D->include_list[C->format_file].modified = 1;

  dreturn("%i", 1);
  return 1;
}

static size_t _GD_DoStringOut(DIRFILE* D, gd_entry_t *S, size_t num_samp,
    const void *data_in)
{
  dtrace("%p, %p, %zi, %p", D, S, num_samp, data_in);

  free(S->e->string);
  S->e->string = strdup(data_in);
  D->include_list[S->format_file].modified = 1;

  dreturn("%i", 1);
  return 1;
}

static size_t _GD_DoFieldOut(DIRFILE *D, const char *field_code,
    off64_t first_frame, off64_t first_samp, size_t num_frames, size_t num_samp,
    gd_type_t data_type, const void *data_in)
{
  gd_entry_t* entry;
  size_t n_wrote = 0;

  dtrace("%p, \"%s\", %lli, %lli, %zi, %zi, 0x%x, %p", D, field_code,
      first_frame, first_samp, num_frames, num_samp, data_type, data_in);

  if (++D->recurse_level >= GD_MAX_RECURSE_LEVEL) {
    _GD_SetError(D, GD_E_RECURSE_LEVEL, 0, NULL, 0, field_code);
    D->recurse_level--;
    dreturn("%zi", 0);
    return 0;
  }

  /* Find the field */
  entry = _GD_FindField(D, field_code);

  if (entry == NULL) { /* No match */
    _GD_SetError(D, GD_E_BAD_CODE, 0, NULL, 0, field_code);
    D->recurse_level--;
    dreturn("%zi", 0);
    return 0;
  }

  switch (entry->field_type) {
    case GD_RAW_ENTRY:
      n_wrote = _GD_DoRawOut(D, entry, first_frame, first_samp, num_frames,
          num_samp, data_type, data_in);
      break;
    case GD_LINTERP_ENTRY:
      n_wrote = _GD_DoLinterpOut(D, entry, first_frame, first_samp, num_frames,
          num_samp, data_type, data_in);
      break;
    case GD_LINCOM_ENTRY:
      n_wrote = _GD_DoLincomOut(D, entry, first_frame, first_samp, num_frames,
          num_samp, data_type, data_in);
      break;
    case GD_BIT_ENTRY:
      n_wrote = _GD_DoBitOut(D, entry, first_frame, first_samp, num_frames,
          num_samp, data_type, data_in);
      break;
    case GD_MULTIPLY_ENTRY:
      _GD_SetError(D, GD_E_BAD_PUT_FIELD, 0, NULL, 0, field_code);
      break;
    case GD_PHASE_ENTRY:
      n_wrote = _GD_DoPhaseOut(D, entry, first_frame, first_samp, num_frames,
          num_samp, data_type, data_in);
      break;
    case GD_CONST_ENTRY:
      n_wrote = _GD_DoConstOut(D, entry, data_type, data_in);
      break;
    case GD_STRING_ENTRY:
      n_wrote = _GD_DoStringOut(D, entry, num_samp, data_in);
      break;
    case GD_NO_ENTRY:
      _GD_InternalError(D);
      break;
  }

  D->recurse_level--;
  dreturn("%zi", n_wrote);
  return n_wrote;
}

/* this function is little more than a public boilerplate for _GD_DoFieldOut */
size_t putdata64(DIRFILE* D, const char *field_code, off64_t first_frame,
    off64_t first_samp, size_t num_frames, size_t num_samp, gd_type_t data_type,
    const void *data_in)
{
  size_t n_wrote = 0;

  dtrace("%p, \"%s\", %lli, %lli, %zi, %zi, 0x%x, %p", D, field_code,
      first_frame, first_samp, num_frames, num_samp, data_type, data_in);

  if (D->flags & GD_INVALID) {/* don't crash */
    _GD_SetError(D, GD_E_BAD_DIRFILE, 0, NULL, 0, NULL);
    dreturn("%zi", 0);
    return 0;
  }

  if ((D->flags & GD_ACCMODE) != GD_RDWR) {
    _GD_SetError(D, GD_E_ACCMODE, 0, NULL, 0, NULL);
    dreturn("%zi", 0);
    return 0;
  }

  _GD_ClearError(D);

  first_frame -= D->frame_offset;

  n_wrote = _GD_DoFieldOut(D, field_code, first_frame, first_samp, num_frames,
      num_samp, data_type, data_in);

  dreturn("%zi", n_wrote);
  return n_wrote;
}

/* 32(ish)-bit wrapper for the 64-bit version, when needed */
size_t putdata(DIRFILE* D, const char *field_code, off_t first_frame,
    off_t first_samp, size_t num_frames, size_t num_samp, gd_type_t data_type,
    const void *data_in)
{
  return putdata64(D, field_code, first_frame, first_samp, num_frames, num_samp,
      data_type, data_in);
}
/* vim: ts=2 sw=2 et
*/
