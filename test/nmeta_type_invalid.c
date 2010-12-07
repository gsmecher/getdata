/* Requesting the number of fields from an invalid dirfile should fail cleanly */
#include "test.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

int main(void)
{
  const char* filedir = __TEST__ "dirfile";
  int r = 0;
  DIRFILE* D = gd_open(filedir, GD_RDONLY);
  unsigned int n = gd_nmfields_by_type(D, "raw1", GD_STRING_ENTRY);
  int error = gd_error(D);
  gd_close(D);

  CHECKU(n, 0);
  CHECKI(error, GD_E_BAD_DIRFILE);

  return r;
}
