/* Copyright (C) 2016 D. V. Wiebe
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
#include "test.h"

int main(void)
{
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  const char *data = "dirfile/data";
  int32_t c[8];
  int e1, r = 0;
  size_t n;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0777);

  MAKEFORMATFILE(format,
    "indir INDIR data carray\n"
    "carray CONST INT32 10\n"
    "data RAW UINT8 8\n");
  MAKEDATAFILE(data, int32_t, i, 256);

  D = gd_open(filedir, GD_RDONLY);
  n = gd_getdata(D, "indir", 1, 0, 1, 0, GD_INT32, &c);
  CHECKU(n, 0);
  e1 = gd_error(D);
  CHECKI(e1, GD_E_BAD_FIELD_TYPE);

  gd_discard(D);

  unlink(data);
  unlink(format);
  rmdir(filedir);

  return r;
}
