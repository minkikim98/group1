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
	// pid_t arr[]={grandchild_id};
	// pid_t *sample;
	// sample = &arr[0]; //can sample be a int array and not char sample[]?
    //
	// //Write the return value of exec in.
  	// int handle, byte_cnt;
    //
	// // CHECK (create ("testgrandchild.txt", sizeof sample - 1), "create \"testgrandchild.txt\"");
	// CHECK ((handle = open ("testgrandchild.txt")) > 1, "open \"testgrandchild.txt\"");
    //
	// byte_cnt = write (handle, sample, sizeof sample - 1);
	// if (byte_cnt != sizeof sample - 1)
	//   fail ("write() returned %d instead of %zu", byte_cnt, sizeof sample - 1);
    return grandchild_id;
}
