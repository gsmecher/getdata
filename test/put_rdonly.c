/* Attempt to write to a read only dirfile */
#include "test.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

int main(void)
{
  const char* filedir = __TEST__ "dirfile";
  const char* format = __TEST__ "dirfile/format";
  const char* data = __TEST__ "dirfile/data";
  const char* format_data = "data RAW UINT8 8\n";
  unsigned char c[8];
  int fd, n, error, unlink_data, r = 0;
  DIRFILE *D;

  memset(c, 0, 8);
  mkdir(filedir, 0777);

  fd = open(format, O_CREAT | O_EXCL | O_WRONLY, 0666);
  write(fd, format_data, strlen(format_data));
  close(fd);

  D = gd_open(filedir, GD_RDONLY | GD_UNENCODED);
  n = gd_putdata(D, "data", 5, 0, 1, 0, GD_UINT8, c);
  error = gd_error(D);

  gd_close(D);

  unlink_data = unlink(data);
  unlink(format);
  rmdir(filedir);

  CHECKI(unlink_data, -1);
  CHECKI(n,0);
  CHECKI(error,GD_E_ACCMODE);
  return r;
}
