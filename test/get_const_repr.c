/* Attempt to read constant with repr */
#include "test.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <math.h>

int main(void)
{
  const char* filedir = __TEST__ "dirfile";
  const char* format = __TEST__ "dirfile/format";
  const char* format_data = "const CONST COMPLEX128 8.3;9.2\n";
  double c;
  int fd, n, error, r = 0;
  DIRFILE *D;

  mkdir(filedir, 0777);

  fd = open(format, O_CREAT | O_EXCL | O_WRONLY, 0666);
  write(fd, format_data, strlen(format_data));
  close(fd);

  D = gd_open(filedir, GD_RDONLY | GD_VERBOSE);
  n = gd_get_constant(D, "const.m", GD_FLOAT64, &c);
  error = gd_error(D);

  gd_close(D);

  unlink(format);
  rmdir(filedir);

  CHECKI(error, 0);
  CHECKI(n, 0);
  CHECKF(c, sqrt(8.3 * 8.3 + 9.2 * 9.2));

  return r;
}
