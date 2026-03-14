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
/* Test overwrite within the in-progress (unflushed) write_buf.
 * Uses 16-byte frames so 8 samples stay buffered; overwrites 2 of them. */
#include "test.h"

int main(void)
{
#if ! (defined TEST_ZSTD) || ! (defined USE_ZSTD)
  return 77;
#else
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  const char *data = "dirfile/data.zst";
  /* 8 samples written first, then 2 overwritten at offset 2 */
  uint8_t c[8]  = { 10, 11, 12, 13, 14, 15, 16, 17 };
  uint8_t ov[2] = { 99, 98 };
  uint8_t d[8];
  int i, n1, n2, m1, m2, e1, e2, e3, e4, e5, unlink_data, r = 0;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  /* 16-byte frames, spf=1, UINT8: 8 samples fit in one frame (< 16 bytes) */
  MAKEFORMATFILE(format, "/ENCODING zstd 16\ndata RAW UINT8 1\n");

  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);

  /* Write 8 samples at frame 0 -- stays in write_buf, not yet committed */
  n1 = gd_putdata(D, "data", 0, 0, 0, 8, GD_UINT8, c);
  e1 = gd_error(D);
  CHECKI(e1, GD_E_OK);
  CHECKI(n1, 8);

  /* Overwrite 2 samples at frame 2: write_pos=2 < current_end=8
   * → overwrite-in-progress-frame path (EndFrame + RewriteFrom) */
  n2 = gd_putdata(D, "data", 2, 0, 0, 2, GD_UINT8, ov);
  e2 = gd_error(D);
  CHECKI(e2, GD_E_OK);
  CHECKI(n2, 2);

  /* Read back without closing */
  m1 = gd_getdata(D, "data", 0, 0, 0, 8, GD_UINT8, d);
  e3 = gd_error(D);
  CHECKI(e3, GD_E_OK);
  CHECKI(m1, 8);
  for (i = 0; i < 8; ++i) {
    uint8_t expect = (i == 2) ? 99 : (i == 3) ? 98 : c[i];
    CHECKIi(i, d[i], expect);
  }

  e4 = gd_close(D);
  CHECKI(e4, 0);

  /* Reopen and verify scan-after-rewrite */
  D = gd_open(filedir, GD_RDONLY | GD_VERBOSE);
  m2 = gd_getdata(D, "data", 0, 0, 0, 8, GD_UINT8, d);
  e5 = gd_error(D);
  CHECKI(e5, GD_E_OK);
  CHECKI(m2, 8);
  for (i = 0; i < 8; ++i) {
    uint8_t expect = (i == 2) ? 99 : (i == 3) ? 98 : c[i];
    CHECKIi(i, d[i], expect);
  }

  gd_discard(D);

  unlink_data = unlink(data);
  unlink(format);
  rmdir(filedir);
  CHECKI(unlink_data, 0);

  return r;
#endif
}
