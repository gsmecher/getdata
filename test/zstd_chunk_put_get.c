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
/* Test basic chunked put+get: write data spanning two chunks, read it back */
#include "test.h"

int main(void)
{
#if !defined TEST_ZSTD || !defined USE_ZSTD
  return 77;
#else
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  uint8_t c[16], d[16];
  int i, n, m, e1, e2, e3, r = 0;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  for (i = 0; i < 16; ++i)
    c[i] = (uint8_t)(40 + i);

  /* chunk_size=2 frames, spf=4: each chunk holds 8 samples */
  MAKEFORMATFILE(format,
      "/ENCODING zstd\n/CHUNK 2\ndata RAW UINT8 4\n");

  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);

  /* Write 16 samples starting at frame 0 -- spans chunks 0 and 2 */
  n = gd_putdata(D, "data", 0, 0, 0, 16, GD_UINT8, c);
  e1 = gd_error(D);
  CHECKI(e1, GD_E_OK);
  CHECKI(n, 16);

  /* Read them back */
  m = gd_getdata(D, "data", 0, 0, 0, 16, GD_UINT8, d);
  e2 = gd_error(D);
  CHECKI(e2, GD_E_OK);
  CHECKI(m, 16);

  for (i = 0; i < 16; ++i)
    CHECKIi(i, d[i], c[i]);

  e3 = gd_close(D);
  CHECKI(e3, 0);

  rmdirfile();
  return r;
#endif
}
