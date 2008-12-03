/* Test move */
#include "../src/getdata.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>

int main(void)
{
  const char* filedir = __TEST__ "dirfile";
  const char* format = __TEST__ "dirfile/format";
  const char* format1 = __TEST__ "dirfile/format1";
  const char* data = __TEST__ "dirfile/data";
  const char* txtdata = __TEST__ "dirfile/data.txt";
  const char* format_data =
    "/INCLUDE format1\ndata RAW UINT16 11\nENCODING text\n";
  const char* format1_data = "ENCODING none\n";
  int we = 0;
  uint16_t d;
  int fd, i;
  FILE* stream;
  gd_entry_t E;

  mkdir(filedir, 0777);

  fd = open(format, O_CREAT | O_EXCL | O_WRONLY, 0666);
  write(fd, format_data, strlen(format_data));
  close(fd);

  fd = open(format1, O_CREAT | O_EXCL | O_WRONLY, 0666);
  write(fd, format1_data, strlen(format1_data));
  close(fd);

  stream = fopen(txtdata, "w");
  for (i = 0; i < 128; ++i)
    fprintf(stream, "%i\n", i * 0x201);
  fclose(stream);

  DIRFILE* D = dirfile_open(filedir, GD_RDWR | GD_UNENCODED | GD_VERBOSE);
  int ret = dirfile_move(D, "data", 1, 1);
  int error = get_error(D);
  int ge_ret =  get_entry(D, "data", &E);
  dirfile_close(D);

  fd = open(data, O_RDONLY);
  i = 0;

  if (fd >= 0) {
    while (read(fd, &d, sizeof(uint16_t))) {
      if (d != i * 0x201) {
        printf("%i = %4x\n", i, d);
        we++;
      }

      i++;
    }
    close(fd);
  } else
    we = -1;

  unlink(format1);
  unlink(format);
  int unlink_data = unlink(data);
  int unlink_txtdata = unlink(txtdata);
  rmdir(filedir);

  if (ret != 0) {
    fprintf(stderr, "1=%i\n", ret);
    return 1;
  }
  if (error != GD_E_OK) {
    fprintf(stderr, "2=%i\n", error);
    return 1;
  }
  if (ge_ret != 0) {
    fprintf(stderr, "3=%i\n", ge_ret);
    return 1;
  }
  if (E.fragment_index != 1) {
    fprintf(stderr, "4=%i\n", E.fragment_index);
    return 1;
  }
  if (we != 0) {
    fprintf(stderr, "5=%i\n", we);
    return 1;
  }
  if (unlink_data != 0) {
    fprintf(stderr, "6=%i\n", unlink_data);
    return 1;
  }
  if (unlink_txtdata != -1) {
    fprintf(stderr, "7=%i\n", unlink_txtdata);
    return 1;
  }

  return 0;
}
