/* Copyright (C) 2016, 2017 D. V. Wiebe
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
#ifdef ENC_SKIP_TEST
  return 77;
#else
  const char *filedir = "dirfile";
  int e1, e2, e3, e4, r = 0;
  DIRFILE *D;
  const uint32_t data_in[8] = { 205, 6, 210, 100, 0, 1, 250, 3 };
  uint32_t data_out[8];
  int i = 0;

  rmdirfile();

  D = gd_open(filedir, GD_RDWR | GD_CREAT | GD_EXCL | GD_ENC_ENCODED
      | GD_VERBOSE);

  e1 = gd_add_spec(D, "data RAW UINT32 1", 0);
  CHECKI(e1, 0);

  e2 = gd_alter_chunk_size(D, ENC_CHUNK_SIZE, 0);
  CHECKI(e2, 0);

  e3 = gd_putdata(D, "data", 0, 0, 0, 8, GD_UINT32, data_in);
  CHECKI(e3, 8);

  e4 = gd_getdata(D, "data", 0, 0, 0, 8, GD_UINT32, data_out);
  CHECKI(e4, 8);

  gd_discard(D);

  if (e4 > 8)
    e4 = 8;
  for (i = 0; i < e4; ++i)
    CHECKUi(i, data_out[i], data_in[i]);

  rmdirfile();

  return r;
#endif
}
