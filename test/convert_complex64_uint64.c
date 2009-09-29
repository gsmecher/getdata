/* Attempt to read COMPLEX64 as UINT64 */
#include "../src/getdata.h"

#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

int main(void)
{
  const char* filedir = __TEST__ "dirfile";
  const char* format = __TEST__ "dirfile/format";
  const char* format_data = "data CONST UINT64 0\n";
  uint64_t c = 0;
  float complex d = 8;
  int fd;

  mkdir(filedir, 0777);

  fd = open(format, O_CREAT | O_EXCL | O_WRONLY, 0666);
  write(fd, format_data, strlen(format_data));
  close(fd);

  DIRFILE* D = dirfile_open(filedir, GD_RDWR);
  int n1 = put_constant(D, "data", GD_COMPLEX64, &d);
  int n2 = get_constant(D, "data", GD_UINT64, &c);
  int error = get_error(D);

  dirfile_close(D);

  unlink(format);
  rmdir(filedir);

  if (error)
    return 1;
  if (n1 != 0)
    return 1;
  if (n2 != 0)
    return 1;
  if (c != 8)
    return 1;

  return 0;
}
