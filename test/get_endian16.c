/* Attempt to read UINT16 with the opposite endianness */
#include "../src/getdata.h"

#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

static int BigEndian(void)
{
  union {
    long int li;
    char ch[sizeof(long int)];
  } un;
  un.li = 1;
  return (un.ch[sizeof(long int) - 1] == 1);
}

int main(void)
{
  const char* filedir = __TEST__ "dirfile";
  const char* format = __TEST__ "dirfile/format";
  const char* data = __TEST__ "dirfile/data";
  char format_data[1000];
  uint16_t c = 0;
  uint16_t data_data[128];
  int fd;
  const int big_endian = BigEndian();

  mkdir(filedir, 0777); 

  sprintf(format_data, "data RAW UINT16 1\nENDIAN %s\n", (big_endian)
      ? "little" : "big");

  for (fd = 0; fd < 128; ++fd)
    data_data[fd] = (uint16_t)(fd * (0x0201));

  fd = open(format, O_CREAT | O_EXCL | O_WRONLY, 0666);
  write(fd, format_data, strlen(format_data));
  close(fd);

  fd = open(data, O_CREAT | O_EXCL | O_WRONLY, 0666);
  write(fd, data_data, 128 * sizeof(uint16_t));
  close(fd);

  DIRFILE* D = dirfile_open(filedir, GD_RDONLY);
  int n = getdata(D, "data", 5, 0, 1, 0, GD_UINT16, &c);
  int error = D->error;

  dirfile_close(D);

  unlink(data);
  unlink(format);
  rmdir(filedir);

  if (error)
    return 1;
  if (n != 1)
    return 1;

  if (c != 0x50a)
    return 1;

  return 0;
}
