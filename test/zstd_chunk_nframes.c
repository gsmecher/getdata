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
/* Test gd_nframes with chunking */
#include "test.h"

int main(void)
{
#if !defined TEST_ZSTD || !defined USE_ZSTD
  return 77;
#else
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  uint8_t c[24];
  int i, n, e1, e2, r = 0;
  off_t nf;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  for (i = 0; i < 24; ++i)
    c[i] = (uint8_t)i;

  /* chunk_size=2 frames, spf=4: each chunk holds 8 samples.
   * Write 24 samples = 6 frames = 3 chunks */
  MAKEFORMATFILE(format,
      "/ENCODING zstd\n/CHUNK 2\ndata RAW UINT8 4\n");

  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);
  n = gd_putdata(D, "data", 0, 0, 0, 24, GD_UINT8, c);
  e1 = gd_error(D);
  CHECKI(e1, GD_E_OK);
  CHECKI(n, 24);

  e2 = gd_close(D);
  CHECKI(e2, 0);

  /* Reopen read-only and check nframes */
  D = gd_open(filedir, GD_RDONLY | GD_VERBOSE);
  nf = gd_nframes(D);
  CHECKI(nf, 6);

  gd_discard(D);
  rmdirfile();
  return r;
#endif
}
