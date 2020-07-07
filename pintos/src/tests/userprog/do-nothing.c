/* Does absolutely nothing. */

#include "tests/lib.h"

int
main (int argc UNUSED, char *argv[])// UNUSED)
{
  int i = 0;
  i += argv[2][2] - '1';
  return 162 + i;
}
