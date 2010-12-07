/* Test gd_parent_fragment */
#include "test.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

int main(void)
{
  const char* filedir = __TEST__ "dirfile";
  const char* format = __TEST__ "dirfile/format";
  const char* format1 = __TEST__ "dirfile/format1";
  const char* format2 = __TEST__ "dirfile/format2";
  const char* format_data = "/INCLUDE format1\na CONST UINT8 1\n";
  const char* format1_data = "b CONST UINT8 11\n/INCLUDE format2 UINT8 11";
  const char* format2_data = "c CONST UINT8 11\n";
  int fd, parent, error, r = 0;
  DIRFILE *D;

  mkdir(filedir, 0777);

  fd = open(format, O_CREAT | O_EXCL | O_WRONLY, 0666);
  write(fd, format_data, strlen(format_data));
  close(fd);

  fd = open(format1, O_CREAT | O_EXCL | O_WRONLY, 0666);
  write(fd, format1_data, strlen(format1_data));
  close(fd);

  fd = open(format2, O_CREAT | O_EXCL | O_WRONLY, 0666);
  write(fd, format2_data, strlen(format2_data));
  close(fd);

  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);
  parent = gd_parent_fragment(D, 2);
  error = gd_error(D);
  gd_close(D);

  unlink(format2);
  unlink(format1);
  unlink(format);
  rmdir(filedir);

  CHECKI(error, 0);
  CHECKI(parent, 1);

  return r;
}
