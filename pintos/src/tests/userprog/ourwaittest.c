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
  	// wait(exec ("child"));

  // //Read file
  // 	int handle;
	// int byte_cnt;
	// int *buffer;
  //
	// CHECK ((handle = open ("testgrandchild.txt")) > 1, "open \"testgrandchild.txt\"");
  //
	// buffer = get_boundary_area () - sizeof sample1 / 2;
	// byte_cnt = read (handle, buffer, sizeof sample1 - 1);
	// if (byte_cnt != sizeof sample1 - 1)
	//   fail ("read() returned %d instead of %zu", byte_cnt, sizeof sample1 - 1);

  	//Check
  	// msg ("wait(exec()) = %d", wait (*(buffer+0))); //this is modeled after wait-killed.c, -1
    msg ("wait(exec()) = %d", wait(wait(exec ("child"))));
}
