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
/* Test gd_expire: mid-chunk frame should round down to chunk boundary.
 * chunk_size=2, expire at frame 3 should round to frame 2, deleting
 * only chunk 0 (frames 0-1). */
#include "test.h"

int main(void)
{
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  int e1, e2, e3, e4, i, r = 0;
  off_t bof;
  uint32_t d[8];
  const uint32_t c[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  MAKEFORMATFILE(format,
      "data RAW UINT32 1\n/ENCODING none\n/CHUNK 2\n");

  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);

  e1 = gd_putdata(D, "data", 0, 0, 0, 8, GD_UINT32, c);
  CHECKI(e1, 8);

  e2 = gd_close(D);
  CHECKI(e2, 0);

  /* Reopen and expire at frame 3 (mid-chunk): should round down to frame 2,
   * deleting only chunk 0 (frames 0-1) */
  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);

  e3 = gd_expire(D, 3, 0);
  CHECKI(e3, 0);

  /* BOF should be at frame 2 (first surviving chunk) */
  bof = gd_bof(D, "data");
  CHECKI(bof, 2);

  /* Frames 2-7 should survive (chunk 1 at frame 2 is >= threshold) */
  e4 = gd_getdata(D, "data", 2, 0, 0, 6, GD_UINT32, d);
  CHECKI(e4, 6);

  gd_discard(D);

  for (i = 0; i < 6; ++i)
    CHECKUi(i, d[i], c[2 + i]);

  rmdirfile();
  return r;
}
