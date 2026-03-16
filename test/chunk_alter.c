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
/* Test gd_alter_chunk_size: non-chunked field with existing data -> chunked */
#include "test.h"

int main(void)
{
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  const char *data = "dirfile/data";
  int e1, e2, e3, i, r = 0;
  uint32_t c[8], d[8];
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  MAKEFORMATFILE(format, "data RAW UINT32 1\n/ENCODING none\n");
  MAKEDATAFILE(data, uint32_t, 100 + i, 8);

  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);

  /* Set chunk_size on field with existing data */
  e1 = gd_alter_chunk_size(D, 2, 0);
  CHECKI(e1, 0);

  /* Read back and verify data preserved */
  e2 = gd_getdata(D, "data", 0, 0, 0, 8, GD_UINT32, d);
  CHECKI(e2, 8);

  e3 = gd_error(D);
  CHECKI(e3, GD_E_OK);

  gd_discard(D);

  for (i = 0; i < 8; ++i) {
    c[i] = 100 + i;
    CHECKUi(i, d[i], c[i]);
  }

  rmdirfile();
  return r;
}
