#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include <user/syscall.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
struct lock file_lock;

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}

/**
* Checks VADDR is not NULL, is in userspace, and is mapped.
*/
static bool
is_valid(const void *vaddr, struct thread *t) {
  if (vaddr == NULL) {
    return false;
  }
  if (!is_user_vaddr(vaddr)) {
    return false;
  }
  if (pagedir_get_page(t->pagedir, vaddr) == NULL) {
    return false;
  }
  return true;
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  if (args[0] == SYS_EXIT)
    {
      f->eax = args[1];
      printf ("%s: exit(%d)\n", &thread_current ()->name, args[1]);
      thread_exit ();
    }


  /**
  * Task 3: File operations.
  */
  if (args[0] == SYS_CREATE
    || args[0] == SYS_REMOVE
    || args[0] == SYS_OPEN
    || args[0] == SYS_FILESIZE
    || args[0] == SYS_READ
    || args[0] == SYS_WRITE
    || args[0] == SYS_SEEK
    || args[0] == SYS_TELL
    || args[0] == SYS_CLOSE) {

      lock_acquire(&file_lock);
      struct thread *cur = thread_current();

      if (args[0] == SYS_CREATE) {
        if (!is_valid((void *) args + 1, cur) || !is_valid((void *) args + 2, cur)) {
          lock_release(&file_lock);
          printf ("%s: exit(%d)\n", (char *) &thread_current ()->name, -1);
          thread_exit ();
        }
        if (!is_valid((void *) args[1], cur) || args[1] == 0) {
          lock_release(&file_lock);
          printf ("%s: exit(%d)\n", (char *) &thread_current ()->name, -1);
          thread_exit ();
        }
        int n = 0;
        int m = 50;
        char *tmp = (char *) args[1];
        while (*(tmp + n) != '\0' && n < m) {
          if (!is_valid((void *) args[1] + n, cur)) {
            f->eax = false;
            goto release;
          }
          n ++;
        }
        char file_name[n];
        memcpy((char *) file_name, (char *) args[1], n + 1);
        f->eax = filesys_create((char *) file_name, args[2]);
      } else if (args[0] == SYS_REMOVE) {
        if (!is_valid((void *) args + 1, cur)) {
          lock_release(&file_lock);
          printf ("%s: exit(%d)\n", (char *) &thread_current ()->name, -1);
          thread_exit ();
        }
        if (!is_valid((void *) args[1], cur) || args[1] == 0) {
          lock_release(&file_lock);
          printf ("%s: exit(%d)\n", (char *) &thread_current ()->name, -1);
          thread_exit ();
        }
        int n = 0;
        int m = 50;
        char *tmp = (char *) args[1];
        while (*(tmp + n) != '\0' && n < m) {
          if (!is_valid((void *) args[1] + n, cur)) {
            f->eax = false;
            goto release;
          }
          n ++;
        }
        char file_name[n];
        memcpy((char *) file_name, (char *) args[1], n + 1);
        f->eax = filesys_remove((char *) args[1]);
      } else if (args[0] == SYS_OPEN) {
        if (!is_valid((void *) args + 1, cur)) {
          lock_release(&file_lock);
          printf ("%s: exit(%d)\n", (char *) &thread_current ()->name, -1);
          thread_exit ();
        }
        if (!is_valid((void *) args[1], cur) || args[1] == 0) {
          lock_release(&file_lock);
          printf ("%s: exit(%d)\n", (char *) &thread_current ()->name, -1);
          thread_exit ();
        }
        int n = 0;
        int m = 50;
        char *tmp = (char *) args[1];
        while (*(tmp + n) != '\0' && n < m) {
          if (!is_valid((void *) args[1] + n, cur)) {
            f->eax = false;
            goto release;
          }
          n ++;
        }
        char file_name[n];
        memcpy((char *) file_name, (char *) args[1], n + 1);
        struct file* fp = filesys_open(file_name);
        if (fp == NULL) {
          f->eax = -1;
          goto release;
        }
        int fd = 2;
        while (fd < 128) {
          if (cur->file_descriptors[fd] == NULL) {
            cur->file_descriptors[fd] = fp;
            break;
          }
          fd ++;
        }
        if (fd == 128) {
          f->eax = -1;
          goto release;
        }
        f->eax = fd;
      } else if (args[0] == SYS_FILESIZE) {
        if (!is_valid((void *) args + 1, cur)) {
          lock_release(&file_lock);
          printf ("%s: exit(%d)\n", (char *) &thread_current ()->name, -1);
          thread_exit ();
        }
        int fd = args[1];
        if (fd < 2 ||fd > 127) {
          f->eax = 0;
          goto release;
        }
        f->eax = file_length(cur->file_descriptors[args[1]]);
      } else if (args[0] == SYS_READ) {
        if (!is_valid((void *) args + 1, cur) || !is_valid((void *) args + 2, cur) || !is_valid((void *) args + 3, cur)) {
          lock_release(&file_lock);
          printf ("%s: exit(%d)\n", (char *) &thread_current ()->name, -1);
          thread_exit ();
        }
        if (!is_valid((void *) args[2], cur) || args[2] == 0) {
          lock_release(&file_lock);
          printf ("%s: exit(%d)\n", (char *) &thread_current ()->name, -1);
          thread_exit ();
        }
        int n = 0;
        int size = (int) args[3];
        char *tmp = (char *) args[2];
        for (; n < size; n ++) {
          if (!is_valid((void *) args[2] + n, cur)) {
            f->eax = -1;
            goto release;
          }
        }
        char buffer[n];
        int fd = args[1];
        /** Read from standard input. */
        if (fd == 0) {
          int i;
          for (i = 0; i < size; i ++) {
            buffer[i] = input_getc();
          }
          f->eax = size;
          for (i = 0; i < size; i ++) {
            *(tmp + i) = buffer[i];
          }
          goto release;
        }
        /** Read from a file descriptor. */
        if (fd < 2 ||fd > 127 || cur->file_descriptors[args[1]] == NULL) {
          f->eax = -1;
          goto release;
        }
        f->eax = file_read(cur->file_descriptors[args[1]], buffer, args[3]);
        if (f->eax > 0) {
          int j;
          for (j = 0; j < size; j ++) {
            *(tmp + j) = buffer[j];
          }
        }
      } else if (args[0] == SYS_WRITE) {
        if (args[1] == 1) {
          printf("%s", (char *) args[2]);
          f->eax = args[3];
        } else {
          f->eax = file_write(cur->file_descriptors[args[1]], (void *) args[2], args[3]);
        }
      } else if (args[0] == SYS_SEEK) {
        file_seek(cur->file_descriptors[args[1]], args[2]);
      } else if (args[0] == SYS_TELL) {
        f->eax = file_tell(cur->file_descriptors[args[1]]);
      } else if (args[0] == SYS_CLOSE) {
        file_close(cur->file_descriptors[args[1]]);
        cur->file_descriptors[args[1]] = NULL;
      }


      release:
        lock_release(&file_lock);
    }

}
