/* Frameindex look-up */
#include "test.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <errno.h>

int main(void)
{
  const char* filedir = __TEST__ "dirfile";
  const char* format = __TEST__ "dirfile/format";
  const char* data = __TEST__ "dirfile/data";
  const char* format_data = "data RAW FLOAT64 1\n";
  double d[1000], f1, f2, f3;
  int i, error, r = 0;
  DIRFILE *D;

  mkdir(filedir, 0777);

  for (i = 0; i < 1000; ++i)
    d[i] = sqrt((i+600) / 500.);

  i = open(format, O_CREAT | O_EXCL | O_WRONLY, 0666);
  write(i, format_data, strlen(format_data));
  close(i);

  i = open(data, O_CREAT | O_EXCL | O_WRONLY | O_BINARY, 0666);
  write(i, d, 1000 * sizeof(double));
  close(i);

  D = gd_open(filedir, GD_RDONLY | GD_VERBOSE);
  f1 = gd_framenum(D, "data", 1.09);
  f2 = gd_framenum(D, "data", 1.49);
  f3 = gd_framenum(D, "data", 1.79);
  error = gd_error(D);

  gd_close(D);

  unlink(data);
  unlink(format);
  rmdir(filedir);

  CHECKI(error, 0);
  CHECKF(f1, -5.96730894763915);
  CHECKF(f2, 510.050010695549);
  CHECKF(f3, 1002.04807025292);

  return r;
}
