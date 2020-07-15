/* INSTRUCTIONS:
    Our wait test.
    Tests failure of the wait syscall on an attempted
    grandchild wait if the child that spawned the grandchild has died.  */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include <string.h>
#include "tests/userprog/boundary.h"
#include "tests/userprog/sample1.inc"


void
test_main (void)
{
  /* 1. Call exec a program first and in that program
     it will call exec again on child-simple and that program will return the grandchild pid
     which wait should fail to wait on and return -1 */
    msg ("wait(exec()) = %d", wait(wait(exec ("child"))));
}
