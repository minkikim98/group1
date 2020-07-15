/* Child process run by our grandchild wait test.
   It should spawn another child. And then the parent will have to wait for that grandchild.
   Which should then terminate the process with a
   -1 exit code. */

#include "tests/lib.h"
#include "tests/main.h"
#include <syscall.h>
// #include "tests/userprog/sample.inc"
//The tests are based on write-normal and read-boundary and all the wait tests frankensteined together.

int
main (void)
{
  	pid_t grandchild_id = exec("child-simple"); //returns child simple's pid
    return grandchild_id;
}
