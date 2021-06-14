#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

int g_filesys_malloc = 0;
/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  init_buffer_cache();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  flush_buffer_cache();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size)
{
  /* Original Implementation */

  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  struct inode *inode = get_inode_from_path(name);
  if (inode != NULL) {
    struct dir *subdir = get_subdir_from_path(name);
    char part[NAME_MAX + 1];
    if (!get_last_part(part, &name)) return false;
    if (subdir != NULL) {
      if (inode_is_dir(inode)) {
        struct dir *dirr = dir_open (inode);
        bool emp = is_empty (dirr);
        dir_close (dirr);
        if (emp) {
          bool success = dir_remove(subdir, part);
          dir_close (subdir);
          return success;
        }
        else {
          dir_close (subdir);
          return false;
        }
      } else {
        inode_remove(inode);
        bool success = dir_remove(subdir, part);
        dir_close (subdir);
        return success;
      }
    } else {
      return false;
    }
  } else {
    return false;
  }
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/* Project 3 Task 3 */

bool filesys_create_2 (const char *name, off_t initial_size) {
  struct dir *subdir = get_subdir_from_path(name);
  if (subdir == NULL) return false;

  char part[NAME_MAX + 1];
  if (!get_last_part(part, &name)) return false;
  block_sector_t inode_sector = 0;

  bool success = (subdir != NULL
                && free_map_allocate (1, &inode_sector)
                && inode_create (inode_sector, initial_size)
                && dir_add (subdir, part, inode_sector));
  if (!success && inode_sector != 0) {
    free_map_release (inode_sector, 1);
    return false;
  }
  dir_close (subdir);
  return success;
}

struct fd *
filesys_open_2 (const char *name)
{
  struct inode *inode = get_inode_from_path(name);
  if (inode != NULL) {
    struct fd *fd = (struct fd*) malloc (sizeof (struct fd));
    ASSERT (fd);
    g_filesys_malloc++;
    if (inode_is_dir(inode)) {
      fd -> dir = dir_open(inode);
      fd -> file = NULL;
      return fd;
    } else {
      fd -> dir = NULL;
      fd -> file = file_open(inode);
      return fd;
    }
  } else {
    return NULL;
  }
}
