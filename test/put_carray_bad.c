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
  void *ptr = main;
  int e1, e2, r = 0;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  MAKEFORMATFILE(format, "#\n");

  D = gd_open(filedir, GD_RDWR);
  e1 = gd_put_carray_slice(D, "carray", 10, 1, GD_UINT8, ptr);
  CHECKI(e1, GD_E_BAD_CODE);

  e2 = gd_put_carray(D, "carray", GD_UINT8, ptr);
  CHECKI(e2, GD_E_BAD_CODE);

  unlink(format);
  rmdir(filedir);

  return r;
}
