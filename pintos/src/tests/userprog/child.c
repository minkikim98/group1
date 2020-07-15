/* INSTRUCTIONS:
   This is the child process run by our grandchild wait test (ourwaittest.c).
   It has been simplified from being based on write-normal and read-boundary tests (no more read-write).
   It should spawn another child which is the grandchild.
   And then the parent of child.c will have to wait for that grandchild.
   Which should then terminate the process with a
   -1 exit code. */

#include "tests/lib.h"
#include "tests/main.h"
#include <syscall.h>

int
main (void)
{
  	pid_t grandchild_id = exec("child-simple"); //returns child simple's pid for the parent to wait on
    return grandchild_id;
}
