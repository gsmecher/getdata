/* Parser check */
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
  const char* format_data =
    "parent RAW UINT8 1\n"
    "META parent INDEX CONST UINT8 1\n";
  int fd, error, r = 0;
  DIRFILE *D;

  mkdir(filedir, 0777);

  fd = open(format, O_CREAT | O_EXCL | O_WRONLY, 0666);
  write(fd, format_data, strlen(format_data));
  close(fd);

  D = gd_open(filedir, GD_RDONLY | GD_VERBOSE);
  error = gd_error(D);
  gd_close(D);

  unlink(format);
  rmdir(filedir);

  CHECKI(error,GD_E_OK);
  return r;
}
