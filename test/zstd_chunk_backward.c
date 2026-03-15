/* Copyright (C) 2026 G. Smecher
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
/* Test backward write into an earlier chunk */
#include "test.h"

int main(void)
{
#if !defined TEST_ZSTD || !defined USE_ZSTD
  return 77;
#else
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  uint32_t c, d[8];
  int i, n1, n2, m, e1, e2, e3, e4, r = 0;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  /* chunk_size=2 frames, spf=1: each chunk holds 2 samples.
   * Write at frame 4 (chunk 4), then write at frame 1 (chunk 0). */
  MAKEFORMATFILE(format,
      "/ENCODING zstd\n/CHUNK 2\ndata RAW UINT32 1\n");

  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);

  c = 44;
  n1 = gd_putdata(D, "data", 4, 0, 0, 1, GD_UINT32, &c);
  e1 = gd_error(D);
  CHECKI(e1, GD_E_OK);
  CHECKI(n1, 1);

  c = 11;
  n2 = gd_putdata(D, "data", 1, 0, 0, 1, GD_UINT32, &c);
  e2 = gd_error(D);
  CHECKI(e2, GD_E_OK);
  CHECKI(n2, 1);

  e3 = gd_close(D);
  CHECKI(e3, 0);

  /* Reopen and verify */
  D = gd_open(filedir, GD_RDONLY);

  /* Read chunk 0 (frames 0-1): present */
  m = gd_getdata(D, "data", 0, 0, 0, 2, GD_UINT32, d);
  e4 = gd_error(D);
  CHECKI(e4, GD_E_OK);
  CHECKI(m, 2);
  CHECKXi(0, d[0], 0);   /* frame 0: zero (gap fill within chunk) */
  CHECKXi(1, d[1], 11);  /* frame 1: written data */

  /* Read spanning into chunk 2 (frames 2-3): missing, zero-filled */
  m = gd_getdata(D, "data", 0, 0, 0, 5, GD_UINT32, d);
  e4 = gd_error(D);
  CHECKI(e4, GD_E_OK);
  CHECKI(m, 5);
  CHECKXi(0, d[0], 0);   /* frame 0: zero (gap fill within chunk) */
  CHECKXi(1, d[1], 11);  /* frame 1: written data */
  CHECKXi(2, d[2], 0);   /* frame 2: zero (missing chunk) */
  CHECKXi(3, d[3], 0);   /* frame 3: zero (missing chunk) */
  CHECKXi(4, d[4], 44);  /* frame 4: written data */

  /* Read chunk 4 (frame 4) directly: present */
  m = gd_getdata(D, "data", 4, 0, 0, 1, GD_UINT32, d);
  e4 = gd_error(D);
  CHECKI(e4, GD_E_OK);
  CHECKI(m, 1);
  CHECKXi(4, d[0], 44);

  gd_discard(D);
  rmdirfile();
  return r;
#endif
}
