/* Tests offset functionalities.
1. Open a file -> offset should be at 0
2. Read a byte -> offset should be at 1 -> check character
3. Seek to POS -> offset should be at POS -> check character
4. Seek to POS2 -> offset should be at POS2 -> check character */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  int handle = open ("alphabet.txt");
  if (handle < 2)
    fail ("open() returned %d", handle);

  /* 1. Open a file -> offset should be at 0 */
  int offset = (int) tell(handle);
  if (offset != 0) {
    fail ("Offset is %d but should be 0", offset);
  }

  /* 2. Read a byte -> offset should be at 1 -> check character */
  char buffer[2];
  int bytes_read = read(handle, (void *) buffer, 1);
  if (bytes_read == 0 || buffer[0] != 'a') {
    fail ("Reading the 0 th character failed");
  }
  offset = (int) tell(handle);
  if (offset != 1) {
    fail ("Offset is %d but should be 1", offset);
  }

  /* 3. Seek to POS -> offset should be at POS -> check character */
  int p = 10;
  seek(handle, (unsigned) p);
  offset = (int) tell(handle);
  if (offset != p) {
    fail ("Offset is %d but should be %d", offset, p);
  }
  bytes_read = read(handle, (void *) buffer, 1);
  if (bytes_read == 0 || buffer[0] != 'k') {
    fail ("Reading the %d th character failed", p);
  }

  /* 4. Seek to POS2 -> offset should be at POS2 -> check character */
  p = 2;
  seek(handle, (unsigned) p);
  offset = (int) tell(handle);
  if (offset != p) {
    fail ("Offset is %d but should be %d", offset, p);
  }
  bytes_read = read(handle, (void *) buffer, 1);
  if (bytes_read == 0 || buffer[0] != 'c') {
    fail ("Reading the %d th character failed", p);
  }

}
