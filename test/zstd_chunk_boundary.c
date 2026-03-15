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
/* Test write that crosses a chunk boundary: write straddles two chunks */
#include "test.h"

int main(void)
{
#if !defined TEST_ZSTD || !defined USE_ZSTD
  return 77;
#else
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  uint32_t c[4], d[4];
  int i, n, m, e1, e2, e3, r = 0;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  for (i = 0; i < 4; ++i)
    c[i] = (uint32_t)(100 + i);

  /* chunk_size=2 frames, spf=1: each chunk holds 2 samples.
   * Write 4 samples starting at frame 1 -- crosses boundary at frame 2 */
  MAKEFORMATFILE(format,
      "/ENCODING zstd\n/CHUNK 2\ndata RAW UINT32 1\n");

  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);
  n = gd_putdata(D, "data", 1, 0, 0, 4, GD_UINT32, c);
  e1 = gd_error(D);
  CHECKI(e1, GD_E_OK);
  CHECKI(n, 4);

  e2 = gd_close(D);
  CHECKI(e2, 0);

  /* Reopen and read back */
  D = gd_open(filedir, GD_RDONLY | GD_VERBOSE);
  m = gd_getdata(D, "data", 1, 0, 0, 4, GD_UINT32, d);
  e3 = gd_error(D);
  CHECKI(e3, GD_E_OK);
  CHECKI(m, 4);

  for (i = 0; i < 4; ++i)
    CHECKIi(i, d[i], c[i]);

  gd_discard(D);
  rmdirfile();
  return r;
#endif
}
