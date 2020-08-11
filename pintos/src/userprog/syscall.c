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
#include "filesys/directory.h"
#include "devices/input.h"

#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/inode.h"
// #include "filesys/inode.c"
// #include "filesys/file.c"

static void syscall_handler (struct intr_frame *);
struct lock file_lock;

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}

/*
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

/**
* Checks STRING has valid address for each element until the null terminator.
* Returns the size of the string, or -1 if invalid.
*/
static int
is_valid_string(char *string, struct thread *t) {
  int n = 0;
  while (*(string + n) != '\0') {
    if (!is_valid((void *) string + n, t)) {
      return -1;
    }
    n ++;
  }
  return n;
}

/**
* Checks buffer has valid address for each element.
* Returns true if valid, false if invalid.
*/
static bool
is_valid_buffer(char *buffer, int size, struct thread *t) {
  int n = 0;
  for (; n < size; n ++) {
    if (!is_valid((void *) buffer + n, t)) {
      return false;
    }
  }
  return true;
}

/**
* Checks if a file descriptor (not 0 or 1) is valid.
*/
static bool
is_valid_fd(int fd, struct thread *t) {
  return !(fd < 2 || fd > 127 || t->file_descriptors[fd] == NULL);
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  struct thread *cur = thread_current ();
  uint32_t* args = ((uint32_t*) f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  /*
   * Helper functions to check if user-provided pointer is valid. Functions vary 
   * to account for the type of user pointer provided. 
   */

  inline bool is_bad_p_byte(void *user_p) {
    return !is_valid(user_p, thread_current());
  }
  bool is_bad_str(void *user_p) {
    while (!is_bad_p_byte(user_p)) {
      if (*((char *) user_p) == '\0') return false;
      user_p++;
    }
    return true;
  }
  bool is_bad_fp(void *user_p) {
    for (int i = 0; i < 4; i ++) {
      if (is_bad_p_byte(user_p + i))
        return true;
    }
    return false;
  }

  /*
   * Helper functions to exit thread and print an error code message with exit code. 
   * Some functions can also check conditions, then exit.
   */ 
  void exit_with_code(int exit_code) {
    cur->o_wait_status->o_exit_code = exit_code;
    printf ("%s: exit(%d)\n", (char *) &cur->name, exit_code);
    thread_exit();
  }
  void exit_error(void) {
    exit_with_code(-1);
  }
  inline void exit_if_bad_arg(int i) {
    if (is_bad_fp(&args[i])) exit_error();
  }
  inline void exit_if_bad_str(char* str) {
    if (is_bad_str(str)) exit_error();
  }

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
    //No need to check pointers because it's an int
    exit_if_bad_arg(1);
    f->eax = process_wait(args[1]); 
  }

  if (args[0] == SYS_EXEC) {
    /* Protect file operations in exec by including it in the critical section. */
    exit_if_bad_arg(1);
    exit_if_bad_str((char *) args[1]);
    f->eax = process_execute((char *) args[1]);
  }

  if (args[0] == SYS_CREATE) {
    /* Check if &args[1], &args[2] are valid. */
    if (!is_valid((void *) args + 1, cur) || !is_valid((void *) args + 2, cur)) {
      exit_with_code(-1);
    }
    /* Check if args[1] is valid and is not a null pointer. */
    if (!is_valid((void *) args[1], cur) || args[1] == 0) {
      exit_with_code(-1);
    }
    /* Check every character in args[1] has a valid address until the null terminator. */
    int n = is_valid_string((char *) args[1], cur);
    if (n < 0) {
      f->eax = false;
      return;
    }
    /* Copy over args[1]. */
    char file_name[n];
    memcpy((char *) file_name, (char *) args[1], n + 1);
    /* Call the appropriate filesys function and store the return value. */
    f->eax = filesys_create((char *) file_name, args[2]);
  }

  if (args[0] == SYS_REMOVE) {
    /* Check if &args[1] is valid.*/
    if (!is_valid((void *)args + 1, cur)) {
        exit_with_code(-1);
    }
    /* Check if args[1] is valid and is not a null pointer. */
    if (!is_valid((void *)args[1], cur) || args[1] == 0) {
        exit_with_code(-1);
    }
    /* Check every character in args[1] has a valid address until the null terminator. */
    int n = is_valid_string((char *)args[1], cur);
    if (n < 0) {
      f->eax = false;
      return;
    }
    /* Copy over args[1]. */
    char file_name[n];
    memcpy((char *)file_name, (char *)args[1], n + 1);
    f->eax = filesys_remove((char *)args[1]);
  }

  if (args[0] == SYS_OPEN) {
    /* Check if &args[1] is valid.*/
    if (!is_valid((void *) args + 1, cur)) {
      //lock_release(&file_lock);
      exit_with_code(-1);
    }
    /* Check if args[1] is valid and is not a null pointer. */
    if (!is_valid((void *) args[1], cur) || args[1] == 0) {
      //lock_release(&file_lock);
      exit_with_code(-1);
    }
    /* Check every character in args[1] has a valid address until the null terminator. */
    int n = is_valid_string((char *) args[1], cur);
    if (n < 0) {
      f->eax = false;
      return;
    }
    /* Copy over args[1]. */
    char file_name[n];
    memcpy((char *) file_name, (char *) args[1], n + 1);
    /* Call the appropriate filesys function. */
    struct file* fp = filesys_open(file_name);
    /* Assign a file descriptor. */
    if (fp == NULL) {
      f->eax = -1;
      return;
    }
    int fd = 2; // Reserve 0 and 1 for standard input and output.
    while (fd < 128) {
      if (cur->file_descriptors[fd] == NULL) {
        cur->file_descriptors[fd] = fp;
        break;
      }
      fd ++;
    }
    if (fd == 128) { // All file descriptors for this thread are occupied.
      f->eax = -1;
      return;
    }
    f->eax = fd;
  }

  if (args[0] == SYS_FILESIZE) {
    /* Check if &args[1] is valid.*/
    if (!is_valid((void *) args + 1, cur)) {
      exit_with_code(-1);
    }
    /* Check if the file descriptor is valid. */
    int fd = args[1];
    if (!is_valid_fd(fd, cur)) {
      f->eax = 0;
      return;
    }
    /* Call the appropriate filesys function. */
    f->eax = file_length(cur->file_descriptors[fd]);
  }

  if (args[0] == SYS_READ) {
    /* Check if &args[1], &args[2], &args[3] are valid.*/
    if (!is_valid((void *) args + 1, cur) || !is_valid((void *) args + 2, cur) || !is_valid((void *) args + 3, cur)) {
      exit_with_code(-1);
    }
    /* Check if args[2] is valid and is not a null pointer. */
    if (!is_valid((void *) args[2], cur) || args[2] == 0) {
      exit_with_code(-1);
    }
    /* Check if the buffer is valid. */
    bool valid_buffer = is_valid_buffer((char *) args[2], (int) args[3], cur);
    if (!valid_buffer) {
      f->eax = -1;
      return;
    }

    int fd = args[1];
    char *buffer = (char *) args[2];
    int size = args[3];
    /* Read from standard input if fd == 0. */
    if (fd == 0) {
      int i;
      for (i = 0; i < size; i ++) {
        buffer[i] = input_getc();
      }
      f->eax = size;
      return;
    }
    /* Read from a file descriptor. Check if fd is valid. */
    if (!is_valid_fd(fd, cur)) {
      f->eax = -1;
      return;
    }
    /* Call the appropriate filesys function. */
    f->eax = file_read(cur->file_descriptors[fd], buffer, size);
  }
  
  if (args[0] == SYS_WRITE) {
    /* Check if &args[1], &args[2], &args[3] are valid.*/
    if (!is_valid((void *) args + 1, cur) || !is_valid((void *) args + 2, cur) || !is_valid((void *) args + 3, cur)) {
      exit_with_code(-1);
    }
    /* Check if args[2] is valid and is not a null pointer. */
    if (!is_valid((void *) args[2], cur) || args[2] == 0) {
      exit_with_code(-1);
    }
    /* Check if the buffer is valid. */
    bool valid_buffer = is_valid_buffer((char *) args[2], (int) args[3], cur);
    if (!valid_buffer) {
      f->eax = -1;
      return;
    }

    int fd = args[1];
    char *buffer = (char *) args[2];
    int size = args[3];
    /* Write to standard output if fd == 1. */
    if (fd == 1) {
      int num_buf;
      if (size % 100 == 0) {
        num_buf = size / 100;
      } else {
        num_buf = size / 100 + 1;
      }
      char smaller_buf[100]; // Write to standard output 100 bytes at a time.
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
      return;
    }
    /* Write to a file. Check if fd is valid. */
    if (!is_valid_fd(fd, cur)) {
      f->eax = -1;
      return;
    }
    /* Call the appropriate filesys function. */
    f->eax = file_write(cur->file_descriptors[fd], buffer, size);
  }
  
  if (args[0] == SYS_SEEK) {
    /* Check if &args[1], &args[2] are valid.*/
    if (!is_valid((void *) args + 1, cur) || !is_valid((void *) args + 2, cur)) {
      exit_with_code(-1);
    }
    /* Check if fd is valid. */
    int fd = args[1];
    if (!is_valid_fd(fd, cur)) {
      return;
    }
    /* Call the appropriate filesys function. */
    file_seek(cur->file_descriptors[fd], args[2]);
  }
  
  if (args[0] == SYS_TELL) {
    /* Check if &args[1] is valid.*/
    if (!is_valid((void *) args + 1, cur)) {
      //lock_release(&file_lock);
      exit_with_code(-1);
    }
    /* Check if fd is valid. */
    int fd = args[1];
    if (!is_valid_fd(fd, cur)) {
      f->eax = 0;
      return;
    }
    /* Call the appropriate filesys function. */
    f->eax = file_tell(cur->file_descriptors[fd]);
  }
  
  if (args[0] == SYS_CLOSE) {
    /* Check if &args[1] is valid.*/
    if (!is_valid((void *) args + 1, cur)) {
      exit_with_code(-1);
    }
    /* Check if fd is valid. */
    int fd = args[1];
    if (!is_valid_fd(fd, cur)) {
      return;
    }
    /* Call the appropriate filesys function. */
    file_close(cur->file_descriptors[fd]);
    /* Free the fd for future use. */
    cur->file_descriptors[fd] = NULL;
  }
  
  if (args[0] == SYS_CHDIR) {
    /* NOTE: This syscall should close the previous cwd using dir_close. */
  }
  
  if (args[0] == SYS_MKDIR) {
    /* Check if &args[1] is valid.*/
    if (!is_valid((void *)args + 1, cur)) {
        exit_with_code(-1);
    }
    /* Check if args[1] is valid and is not a null pointer. */
    if (!is_valid((void *)args[1], cur) || args[1] == 0) {
        exit_with_code(-1);
    }
    /* Check every character in args[1] has a valid address until the null terminator. */
    int n = is_valid_string((char *)args[1], cur);
    if (n < 0) {
      f->eax = false;
      return;
    }
    /* Copy over args[1]. */
    char file_name[n];
    memcpy((char *)file_name, (char *)args[1], n + 1);

    // If name is empty, return false.
    if (strlen(file_name) == 0) {
      f->eax = 0;
      return;
    }

    // If name is '/', return false.
    if (file_name == "/") {
      f->eax;
      return;
    }

    // If inode with same name already exists, return false.
    if (get_inode_from_path(file_name) != NULL) {
      f->eax = 0;
      return;
    }

    // Get the directory the inode should be in.
    struct dir *subdir = get_subdir_from_path(file_name);
    if (subdir == NULL) {
      f->eax = 0;
      return;
    }
    subdir_create(file_name, subdir);
  }
  
  if (args[0] == SYS_READDIR) {
    
  }
  
  if (args[0] == SYS_ISDIR) {
    
  }
  
  /* Implemented as part of Proj3 Task 2
   which returns the unique inode number of file associated with a particular file descriptor.

   Returns the inode number of the inode associated with fd, which
   may represent an ordinary file or a directory.
   An inode number persistently identifies a file or directory. It is unique during the file’s existence.
   In Pintos, the sector number of the inode is suitable for use as an inode number.

   According to Sam, the inumber of a file is the disk address (block_sector_t) of its inode. */

  if (args[0] == SYS_INUMBER) {
    // exit_if_bad_arg(1);
    // /* Check if &args[1] is valid.*/
    // if (!is_valid((void *)args + 1, cur)) {
    //     exit_with_code(-1);
    // }
    // /* Check if args[1] is valid and is not a null pointer. */
    // if (!is_valid((void *)args[1], cur) || args[1] == 0) {
    //     exit_with_code(-1);
    // }
    // int fd_to_find = args[1];
    // struct file * list_of_fds = cur->file_descriptors;
    // struct file * file_associated_with_fd = &(list_of_fds[fd_to_find]);
    // struct inode * inode = file_associated_with_fd->inode;
    // block_sector_t inode_number = inode->sector;
    // f->eax = (uint32_t) inode_number;
    // return;
  }

    
  /**
  * Task 3: File operations.
  * Below is a critical section that uses the global lock FILE_LOCK.
  */
 
  // if (args[0] == SYS_CREATE
  //   || args[0] == SYS_REMOVE
  //   || args[0] == SYS_OPEN
  //   || args[0] == SYS_FILESIZE
  //   || args[0] == SYS_READ
  //   || args[0] == SYS_WRITE
  //   || args[0] == SYS_SEEK
  //   || args[0] == SYS_TELL
  //   || args[0] == SYS_CLOSE
  //   || args[0] == SYS_EXEC) {

  //     /* Acquire the lock and obtain the current thread. */
  //     //lock_acquire(&file_lock);
  //     // struct thread *cur = thread_current ();

  //     if (args[0] == SYS_EXEC) {
  //       /* Protect file operations in exec by including it in the critical section. */
  //       exit_if_bad_arg(1);
  //       exit_if_bad_str(args[1]);
  //       f->eax = process_execute((char *) args[1]);
  //     } else if (args[0] == SYS_CREATE) {
  //       /* Check if &args[1], &args[2] are valid. */
  //       if (!is_valid((void *) args + 1, cur) || !is_valid((void *) args + 2, cur)) {
  //         //lock_release(&file_lock);
  //         exit_with_code(-1);
  //       }
  //       /* Check if args[1] is valid and is not a null pointer. */
  //       if (!is_valid((void *) args[1], cur) || args[1] == 0) {
  //         //lock_release(&file_lock);
  //         exit_with_code(-1);
  //       }
  //       /* Check every character in args[1] has a valid address until the null terminator. */
  //       int n = is_valid_string((char *) args[1], cur);
  //       if (n < 0) {
  //           f->eax = false;
  //           goto release;
  //       }
  //       /* Copy over args[1]. */
  //       char file_name[n];
  //       memcpy((char *) file_name, (char *) args[1], n + 1);
  //       /* Call the appropriate filesys function and store the return value. */
  //       f->eax = filesys_create((char *) file_name, args[2]);
  //     } else if (args[0] == SYS_REMOVE) {
  //       /* Check if &args[1] is valid.*/
  //       if (!is_valid((void *) args + 1, cur)) {
  //         //lock_release(&file_lock);
  //         exit_with_code(-1);
  //       }
  //       /* Check if args[1] is valid and is not a null pointer. */
  //       if (!is_valid((void *) args[1], cur) || args[1] == 0) {
  //         //lock_release(&file_lock);
  //         exit_with_code(-1);
  //       }
  //       /* Check every character in args[1] has a valid address until the null terminator. */
  //       int n = is_valid_string((char *) args[1], cur);
  //       if (n < 0) {
  //           f->eax = false;
  //           goto release;
  //       }
  //       /* Copy over args[1]. */
  //       char file_name[n];
  //       memcpy((char *) file_name, (char *) args[1], n + 1);
  //       f->eax = filesys_remove((char *) args[1]);
  //     } else if (args[0] == SYS_OPEN) {
  //       /* Check if &args[1] is valid.*/
  //       if (!is_valid((void *) args + 1, cur)) {
  //         //lock_release(&file_lock);
  //         exit_with_code(-1);
  //       }
  //       /* Check if args[1] is valid and is not a null pointer. */
  //       if (!is_valid((void *) args[1], cur) || args[1] == 0) {
  //         //lock_release(&file_lock);
  //         exit_with_code(-1);
  //       }
  //       /* Check every character in args[1] has a valid address until the null terminator. */
  //       int n = is_valid_string((char *) args[1], cur);
  //       if (n < 0) {
  //           f->eax = false;
  //           goto release;
  //       }
  //       /* Copy over args[1]. */
  //       char file_name[n];
  //       memcpy((char *) file_name, (char *) args[1], n + 1);
  //       /* Call the appropriate filesys function. */
  //       struct file* fp = filesys_open(file_name);
  //       /* Assign a file descriptor. */
  //       if (fp == NULL) {
  //         f->eax = -1;
  //         goto release;
  //       }
  //       int fd = 2; // Reserve 0 and 1 for standard input and output.
  //       while (fd < 128) {
  //         if (cur->file_descriptors[fd] == NULL) {
  //           cur->file_descriptors[fd] = fp;
  //           break;
  //         }
  //         fd ++;
  //       }
  //       if (fd == 128) { // All file descriptors for this thread are occupied.
  //         f->eax = -1;
  //         goto release;
  //       }
  //       f->eax = fd;
  //     } else if (args[0] == SYS_FILESIZE) {
  //       /* Check if &args[1] is valid.*/
  //       if (!is_valid((void *) args + 1, cur)) {
  //         //lock_release(&file_lock);
  //         exit_with_code(-1);
  //       }
  //       /* Check if the file descriptor is valid. */
  //       int fd = args[1];
  //       if (!is_valid_fd(fd, cur)) {
  //         f->eax = 0;
  //         goto release;
  //       }
  //       /* Call the appropriate filesys function. */
  //       f->eax = file_length(cur->file_descriptors[fd]);
  //     } else if (args[0] == SYS_READ) {
  //       /* Check if &args[1], &args[2], &args[3] are valid.*/
  //       if (!is_valid((void *) args + 1, cur) || !is_valid((void *) args + 2, cur) || !is_valid((void *) args + 3, cur)) {
  //         //lock_release(&file_lock);
  //         exit_with_code(-1);
  //       }
  //       /* Check if args[2] is valid and is not a null pointer. */
  //       if (!is_valid((void *) args[2], cur) || args[2] == 0) {
  //         //lock_release(&file_lock);
  //         exit_with_code(-1);
  //       }
  //       /* Check if the buffer is valid. */
  //       bool valid_buffer = is_valid_buffer((char *) args[2], (int) args[3], cur);
  //       if (!valid_buffer) {
  //         f->eax = -1;
  //         goto release;
  //       }

  //       int fd = args[1];
  //       char *buffer = (char *) args[2];
  //       int size = args[3];
  //       /* Read from standard input if fd == 0. */
  //       if (fd == 0) {
  //         int i;
  //         for (i = 0; i < size; i ++) {
  //           buffer[i] = input_getc();
  //         }
  //         f->eax = size;
  //         goto release;
  //       }
  //       /* Read from a file descriptor. Check if fd is valid. */
  //       if (!is_valid_fd(fd, cur)) {
  //         f->eax = -1;
  //         goto release;
  //       }
  //       /* Call the appropriate filesys function. */
  //       f->eax = file_read(cur->file_descriptors[fd], buffer, size);
  //     } else if (args[0] == SYS_WRITE) {
  //       /* Check if &args[1], &args[2], &args[3] are valid.*/
  //       if (!is_valid((void *) args + 1, cur) || !is_valid((void *) args + 2, cur) || !is_valid((void *) args + 3, cur)) {
  //         //lock_release(&file_lock);
  //         exit_with_code(-1);
  //       }
  //       /* Check if args[2] is valid and is not a null pointer. */
  //       if (!is_valid((void *) args[2], cur) || args[2] == 0) {
  //         //lock_release(&file_lock);
  //         exit_with_code(-1);
  //       }
  //       /* Check if the buffer is valid. */
  //       bool valid_buffer = is_valid_buffer((char *) args[2], (int) args[3], cur);
  //       if (!valid_buffer) {
  //         f->eax = -1;
  //         goto release;
  //       }

  //       int fd = args[1];
  //       char *buffer = (char *) args[2];
  //       int size = args[3];
  //       /* Write to standard output if fd == 1. */
  //       if (fd == 1) {
  //         int num_buf;
  //         if (size % 100 == 0) {
  //           num_buf = size / 100;
  //         } else {
  //           num_buf = size / 100 + 1;
  //         }
  //         char smaller_buf[100]; // Write to standard output 100 bytes at a time.
  //         int i = 0;
  //         for (; i < num_buf; i ++) {
  //           if (i == num_buf - 1) {
  //             memcpy(smaller_buf, buffer + i * 100, size - i * 100);
  //             putbuf(smaller_buf, size - i * 100);
  //           } else {
  //             memcpy(smaller_buf, buffer + i * 100, 100);
  //             putbuf(smaller_buf, 100);
  //           }
  //         }
  //         goto release;
  //       }
  //       /* Write to a file. Check if fd is valid. */
  //       if (!is_valid_fd(fd, cur)) {
  //         f->eax = -1;
  //         goto release;
  //       }
  //       /* Call the appropriate filesys function. */
  //       f->eax = file_write(cur->file_descriptors[fd], buffer, size);
  //     } else if (args[0] == SYS_SEEK) {
  //       /* Check if &args[1], &args[2] are valid.*/
  //       if (!is_valid((void *) args + 1, cur) || !is_valid((void *) args + 2, cur)) {
  //         //lock_release(&file_lock);
  //         exit_with_code(-1);
  //       }
  //       /* Check if fd is valid. */
  //       int fd = args[1];
  //       if (!is_valid_fd(fd, cur)) {
  //         goto release;
  //       }
  //       /* Call the appropriate filesys function. */
  //       file_seek(cur->file_descriptors[fd], args[2]);
  //     } else if (args[0] == SYS_TELL) {
  //       /* Check if &args[1] is valid.*/
  //       if (!is_valid((void *) args + 1, cur)) {
  //         //lock_release(&file_lock);
  //         exit_with_code(-1);
  //       }
  //       /* Check if fd is valid. */
  //       int fd = args[1];
  //       if (!is_valid_fd(fd, cur)) {
  //         f->eax = 0;
  //         goto release;
  //       }
  //       /* Call the appropriate filesys function. */
  //       f->eax = file_tell(cur->file_descriptors[fd]);
  //     } else if (args[0] == SYS_CLOSE) {
  //       /* Check if &args[1] is valid.*/
  //       if (!is_valid((void *) args + 1, cur)) {
  //         //lock_release(&file_lock);
  //         exit_with_code(-1);
  //       }
  //       /* Check if fd is valid. */
  //       int fd = args[1];
  //       if (!is_valid_fd(fd, cur)) {
  //         goto release;
  //       }
  //       /* Call the appropriate filesys function. */
  //       file_close(cur->file_descriptors[fd]);
  //       /* Free the fd for future use. */
  //       cur->file_descriptors[fd] = NULL;
  //     }


  //     /* Release the lock and exit the critical section. */
  //       char *c = "I am Mabel!!!";
  //     release:
  //     c++;
  //       //lock_release(&file_lock);
  //   } 

}
