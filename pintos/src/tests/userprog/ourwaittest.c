/* Our wait test. Tests failure on a grandchild wait.  */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include <string.h>
#include "tests/userprog/boundary.h"
#include "tests/userprog/sample1.inc"


void
test_main (void)
{
  //I wonder if this will work. Call exec a program first and in that program
  // it will call exec again and that program will return the grandchild pid
  // which wiat should wait on and return -1.
    msg ("wait(exec()) = %d", wait(wait(exec ("child"))));
}
