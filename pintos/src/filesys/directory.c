#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* A directory. */
struct dir
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Project 3 Task 3 Segment */
// static void get_dir_lock(struct dir *dir) {
//   struct inode *inode = dir->inode;
//   lock_acquire(inode->inode_dir_lock);
// }

// static void rel_dir_lock(struct dir *dir) {
//   lock_release(dir->inode->inode_dir_lock);
// }
/* End Segment */

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry));
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode)
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL;
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir)
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir)
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir)
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  get_dir_lock(dir->inode);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use && !strcmp (name, e.name))
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  
  release_dir_lock(dir->inode);
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode)
{
  // printf("dir is: %x\n", dir);
  // printf("name is: %s\n", name);
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name)
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e)
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        }
    }
  return false;
}

/* Project 3 Task 3 Segment */

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
   next call will return the next file name part. Returns 1 if successful, 0 at
   end of string, -1 for a too-long file name part. */

static int
get_next_part (char part[NAME_MAX + 1], const char **srcp) {
  // printf("in get_next_part\n");
  const char *src = *srcp;
  char *dst = part;
  /* Skip leading slashes.  If it’s all slashes, we’re done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;
  /* Copy up to NAME_MAX character from SRC to DST.  Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++; 
  }
  *dst = '\0';
  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

/* Checks to see if *SRCP is about to read the last part. 
   Reverts *SRCP to original state afterwards. */

static bool is_last_part(const char **srcp) {
  const char *saved = *srcp;
  char part[NAME_MAX + 1];
  int status;

  get_next_part(part, srcp);
  status = get_next_part(part, srcp);
  *srcp = saved;
  if (status == 0) return true;
  else return false;
}

static bool is_relative(char *path) {
  if (*path == '/') return false;
  else return true;
}

/* Opens the directory the path is referring to. Assumes callee closes directory. */
struct dir *get_dir_from_path(char *path) {

  ASSERT (path != NULL);

  struct thread *t = thread_current();
  struct dir *cur_dir;
  struct inode *next = NULL;
  char part[NAME_MAX + 1];

  const char *saved_path = path;

  if (path == "") return dir_reopen(t->cwd);

  // Check if path is relative or absolute.
  if (is_relative(path)) cur_dir = dir_reopen(t->cwd);
  else cur_dir = dir_open_root();  

  // Iterate through path and find subdirectories.
  
  int status = 0;
  while (1) {
    status = get_next_part(part, &saved_path);
    // Name length was too long.
    if (status == -1) {
      return NULL;
    }
    // Reached end of path successfully. 
    else if (status == 0) {
      return cur_dir;
    }
    // Got part of the path successfully.
    else {
      if (dir_lookup(cur_dir, saved_path, &next)) {
        // Check if result is a directory.
        if (!inode_is_dir(next)) return NULL;

        dir_close(cur_dir);
        cur_dir = dir_open(next);
      }
      // Couldn't find next part of path in directory. Return NULL.
      else {
        return NULL;
      }
    }
  }
}

struct inode *get_inode_from_path(char *path) {

  ASSERT (path != NULL);

  struct thread *t = thread_current();
  struct dir *cur_dir;
  struct inode *next = NULL;
  char part[NAME_MAX + 1];

  const char *saved_path = path;

  // Check if path is relative or absolute.
  if (is_relative(path)) cur_dir = dir_reopen(t->cwd);
  else cur_dir = dir_open_root();  

  // Iterate through path and find subdirectories.
  int status = 0;
  while (1) {
    status = get_next_part(part, &saved_path);
    // Name length was too long.
    if (status == -1) {
      return NULL;
    }
    // Reached end of path successfully. 
    else if (status == 0) {
      return next;
    }
    // Got part of the path successfully.
    else {
      if (cur_dir != NULL && dir_lookup(cur_dir, saved_path, &next)) {
        dir_close(cur_dir);
        // If next was not a directory, our next iteration will check if cur_dir was set to NULL.
        cur_dir = dir_open(next);
      }
      // Couldn't find next part of path in directory. Return NULL.
      else {
        return NULL;
      }
    }
  }
}

/* Returns the subdirectory of the path. For example, returns "a/b/c" on an input of "a/b/c/d" */
struct dir *get_subdir_from_path(char *path) {
  int path_len = strlen(path);
  char copy[PATH_MAX + 1];
  strlcpy(copy, path, path_len);

  char *end = copy + path_len - 1;

  while (*end == '/') {
    end--;
    path_len--;
  }
  while (*end != '/') {
    end--;
    path_len--;
  }
  copy[path_len] = '\0';
  return get_dir_from_path(copy);
}

/* End Segment */