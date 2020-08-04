/* INSTRUCTIONS:
    Our wait test.
    Tests failure of the wait syscall on an attempted
    grandchild wait if the child that spawned the grandchild has died.
    Relevant files: this file (ourwaittest.c), child.c, child-simple.c and ourwaittest.ck. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include <string.h>



void
test_main (void)
{
  /* 1. Call exec a program first and in that program
     it will call exec again on child-simple and that program will return the grandchild pid
     which wait should fail to wait on and return -1 */
    msg ("wait(exec()) = %d", wait(wait(exec ("child"))));
}
