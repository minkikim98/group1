#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include <user/syscall.h>
#include <console.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"

#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

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

  /*
   * Helper functions for checking user-defined pointers. Variations for pointer
   * type and whether we need to check a certain number of bytes following that 
   * pointer. 
   */
  inline bool is_bad_p_byte(void *user_p)
  {
    return !is_valid(user_p, thread_current());
  }

  bool is_bad_str_len(void *user_p, int* length)
  {
    *length = 0;
    while (!is_bad_p_byte(user_p))
    {
      (*length) ++;
      if (*((char *) user_p) == '\0') return false;
      user_p++;
    }
    return true;
  }

  bool is_bad_str(void *user_p)
  {
    int placeHolder = 0;
    return is_bad_str_len(user_p, &placeHolder);
  }

  bool is_bad_fp(void *user_p)
  {
    for (int i = 0; i < 4; i ++)
    {
      if (is_bad_p_byte(user_p + i))
        return true;
    }
    return false;
  }

  /*
   * Helper functions for exiting. Variations for whether to exit with a
   * certain code or with error, in which case we return with -1.
   */
  void exit_with_code(int exit_code)
  {
    thread_current()->o_wait_status->o_exit_code = exit_code;
    printf ("%s: exit(%d)\n", &thread_current ()->name, exit_code);
    thread_exit();
  }
  void exit_error(void)
  {
    exit_with_code(-1);
  }
  inline void exit_if_bad_arg(int i)
  {
    if (is_bad_fp(&args[i])) exit_error();
  }
  inline void exit_if_bad_str(char* str)
  {
    if (is_bad_str(str)) exit_error();
  }

  // Check the syscall number.
  exit_if_bad_arg(0);

  if (args[0] == SYS_PRACTICE) {
    exit_if_bad_arg(1);
    f->eax = args[1] + 1;
    return;
  }

  if (args[0] == SYS_HALT) {
    shutdown_power_off();
  }

  if (args[0] == SYS_EXIT) {
    exit_if_bad_arg(1);
    f->eax = args[1];
    exit_with_code(args[1]);
  }

  if (args[0] == SYS_WAIT) {
    exit_if_bad_arg(1);
    f->eax = process_wait(args[1]);
  }

  void exit_release_if_bad_arg(int i)
  {
    if (is_bad_fp(&args[i]))
    {
      lock_release(&file_lock);
      exit_error();
    }
  }
  void exit_release_if_bad_str(char* ptr, int* len)
  {
    if (is_bad_str_len(ptr, len))
    {
      lock_release(&file_lock);
      exit_error();
    }
  }
  /**
  * Task 3: File operations.
  */
  struct thread *cur = thread_current ();

  if (args[0] == SYS_CREATE
    || args[0] == SYS_REMOVE
    || args[0] == SYS_OPEN)
  {
    lock_acquire(&file_lock);
    exit_release_if_bad_arg(1);
    int len;
    exit_release_if_bad_str((char *) args[1], &len);
    char file_name[len];
    memcpy((char *) file_name, (char *) args[1], len);
    if (args[0] == SYS_CREATE)
    {
      exit_release_if_bad_arg(2);
      f->eax = filesys_create((char *) file_name, args[2]);
    }
    else if (args[0] == SYS_REMOVE)
    {
      f->eax = filesys_remove((char *) args[1]);
    }
    else if (args[0] == SYS_OPEN)
    {
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
    }
  }
  else if(args[0] == SYS_FILESIZE
    || args[0] == SYS_READ
    || args[0] == SYS_WRITE
    || args[0] == SYS_SEEK
    || args[0] == SYS_TELL
    || args[0] == SYS_CLOSE
    || args[0] == SYS_EXEC) {

      lock_acquire(&file_lock);
      struct thread *cur = thread_current ();
      if (args[0] == SYS_FILESIZE) {
        exit_release_if_bad_arg(1);
        int fd = args[1];
        if (fd < 2 ||fd > 127) {
          f->eax = 0;
          goto release;
        }
        f->eax = file_length(cur->file_descriptors[args[1]]);
      } else if (args[0] == SYS_READ) {
        exit_release_if_bad_arg(1);
        exit_release_if_bad_arg(2);
        exit_release_if_bad_arg(3);
        if (!is_valid((void *) args[2], cur) || args[2] == 0) {
          lock_release(&file_lock);
          exit_with_code(-1);
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
        char *buffer = (char *) args[2];
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
      } else if (args[0] == SYS_WRITE) {
        exit_release_if_bad_arg(1);
        exit_release_if_bad_arg(2);
        exit_release_if_bad_arg(3);
        if (!is_valid((void *) args[2], cur) || args[2] == 0) {
          lock_release(&file_lock);
          exit_with_code(-1);
        }
        int n = 0;
        int size = (int) args[3];
        for (; n < size ; n ++) {
          if (!is_valid((void *) args[2] + n, cur)) {
            f->eax = -1;
            goto release;
          }
        }
        char *buffer = (char *) args[2];
        int fd = args[1];
        /* Write to standard output. */
        if (fd == 1) {
          int num_buf;
          if (size % 100 == 0) {
            num_buf = size / 100;
          } else {
            num_buf = size / 100 + 1;
          }
          char smaller_buf[100];
          int i = 0;
          for (; i < num_buf; i ++) {
            if (i == num_buf - 1) {
              memcpy(smaller_buf, buffer + i * 100, size - i * 100);
              putbuf(smaller_buf, size - i * 100);
            } else {
              memcpy(smaller_buf, buffer + i * 100, 100);
              putbuf(smaller_buf, 100);
            }
          }
          goto release;
        }
        /* Write to a file. */
        if (fd < 2 ||fd > 127 || cur->file_descriptors[args[1]] == NULL) {
          f->eax = -1;
          goto release;
        }
        f->eax = file_write(cur->file_descriptors[args[1]], buffer, size);
      } else if (args[0] == SYS_SEEK) {
        exit_release_if_bad_arg(1);
        exit_release_if_bad_arg(2);
        if (args[1] < 2 || args[1] > 127 || cur->file_descriptors[args[1]] == NULL) {
          goto release;
        }
        file_seek(cur->file_descriptors[args[1]], args[2]);
      } else if (args[0] == SYS_TELL) {
        exit_release_if_bad_arg(1);
        if (args[1] < 2 || args[1] > 127 || cur->file_descriptors[args[1]] == NULL) {
          f->eax = 0;
          goto release;
        }
        f->eax = file_tell(cur->file_descriptors[args[1]]);
      } else if (args[0] == SYS_CLOSE) {
        exit_release_if_bad_arg(1);
        if (args[1] < 2 || args[1] > 127 || cur->file_descriptors[args[1]] == NULL) {
          goto release;
        }
        file_close(cur->file_descriptors[args[1]]);
        cur->file_descriptors[args[1]] = NULL;
      }


    }
    release:
      if (lock_held_by_current_thread (&file_lock))
        lock_release(&file_lock);

}
