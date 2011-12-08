/* Copyright (C) 2011 D. V. Wiebe
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
  const char *format_data =
    "data1 RAW UINT8 1\n"
    "data2 RAW UINT8 1\n"
    "/HIDDEN data2\n"
    "data3 RAW UINT8 1\n";
  int fd, i, error, r = 0;
  const char **field_list;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0777);

  fd = open(format, O_CREAT | O_EXCL | O_WRONLY, 0666);
  write(fd, format_data, strlen(format_data));
  close(fd);

  D = gd_open(filedir, GD_RDONLY | GD_VERBOSE);
  field_list = gd_field_list(D);

  error = gd_error(D);

  CHECKI(error, 0);
  CHECKPN(field_list);

  for (i = 0; ; ++i) {
    if (field_list[i] == NULL)
      break;

    if (strcmp(field_list[i], "data1") == 0)
      continue;
    else if (strcmp(field_list[i], "data3") == 0)
      continue;
    else if (strcmp(field_list[i], "INDEX") == 0)
      continue;

    fprintf(stderr, "field_list[%i] = \"%s\"\n", i, field_list[i]);
    r = 1;
  }

  CHECKI(i, 3);

  gd_close(D);
  unlink(format);
  rmdir(filedir);

  return r;
}
