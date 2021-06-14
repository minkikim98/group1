#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/synch.h"

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

int g_dir_calloc = 0, g_dir_freed = 0;

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create_wild (sector, entry_cnt * sizeof (struct dir_entry), true);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode)
{
  ASSERT (inode);

  struct dir *dir = calloc (1, sizeof *dir);
  g_dir_calloc ++;
  if (g_dir_calloc % 1000 == 0) {}
  ASSERT (dir);
  dir->inode = inode;
  dir->pos = 0;
  return dir;
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
  ASSERT (dir);
  if (!inode_is (dir->inode))
  {
    return NULL;
  }
  struct inode *inode = inode_reopen (dir->inode);
  return dir_open (inode);
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir)
{
  if (dir != NULL)
    {
      ASSERT (dir->inode);
      if (inode_is (dir->inode))
        inode_close (dir->inode);
      free (dir);
      g_dir_freed++;
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir)
{
  ASSERT (dir);
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup_unsynched (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

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
  return false;
}

static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp)
{
  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  bool result;
  get_dir_lock (dir_get_inode (dir));
  result = lookup_unsynched (dir, name, ep, ofsp);
  release_dir_lock (dir_get_inode (dir));
  return result;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup_unsynched (const struct dir *dir, const char *name,
            struct inode **inode)
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  bool dotdot = strcmp(name, "..") == 0;
  if (lookup_unsynched (dir, name, &e, NULL))
  {
    *inode = inode_open (e.inode_sector);
    ASSERT (*inode);
    ASSERT (inode_is (*inode));
  }
  else
  {
    ASSERT (dotdot != true);
    *inode = NULL;
  }

  return *inode != NULL;
}

bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode)
{
  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  bool result;
  get_dir_lock (dir_get_inode (dir));
  result = dir_lookup_unsynched (dir, name, inode);
  release_dir_lock (dir_get_inode (dir));
  return result;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add_unsynched (struct dir *dir, const char *name, block_sector_t inode_sector)
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
  if (lookup_unsynched (dir, name, NULL, NULL))
  {
    goto done;
  }

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

bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  bool result;
  get_dir_lock (dir_get_inode (dir));
  result = dir_add_unsynched (dir, name, inode_sector);
  release_dir_lock (dir_get_inode (dir));
  return result;
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
  get_dir_lock (dir_get_inode (dir));

  /* Find directory entry. */
  if (!lookup_unsynched (dir, name, &e, &ofs))
  {
    release_dir_lock (dir_get_inode (dir));
    return false;
  }
  release_dir_lock (dir_get_inode (dir));

  /* Open inode. */
  inode = inode_open (e.inode_sector);

  ASSERT (inode);
  if (inode == NULL)
  {
    return false;
  }

  /* Erase directory entry. */
  e.in_use = false;

  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
  {
    ASSERT (false);
    inode_remove (inode);
    return false;
  }

  /* Remove inode. */
  inode_remove (inode);
  inode_close (inode);
  return true;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;
  ASSERT (dir);
  ASSERT (dir->inode);
  get_dir_lock (dir_get_inode (dir));
  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e)
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          release_dir_lock (dir_get_inode (dir));
          return true;
        }
    }
  release_dir_lock (dir_get_inode (dir));
  return false;
}

/* Project 3 Task 3 Segment */

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
   next call will return the next file name part. Returns 1 if successful, 0 at
   end of string, -1 for a too-long file name part. */

static int
get_next_part (char part[NAME_MAX + 1], const char **srcp) {
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

/* Gets last part of path. */

bool get_last_part(char part[NAME_MAX + 1], const char **srcp) {
  int status;
  while (1) {
    status = get_next_part(part, srcp);
    if (status == -1) {
      return false;
    }
    else if (status == 0) {
      return true;
    }
  }
}

static bool is_relative(char *path) {
  if (*path == '/') return false;
  else return true;
}


/* Opens the inode the path is referring to, whether file or directory. */
struct inode *get_inode_from_path(char *path) {
  ASSERT (path != NULL);

  struct thread *t = thread_current();
  struct dir *cur_dir;
  struct inode *next = NULL;
  char part[NAME_MAX + 1];

  const char *saved_path = path;

  if (*path == '\0') {
    struct inode *cwd_i = dir_get_inode(t->cwd);
    if (!inode_is(cwd_i)) return NULL;
    ASSERT (inode_is (cwd_i));
    if (cwd_i == NULL) return NULL;
    if (to_be_removed(cwd_i)) {
      return NULL;
    }
    inode_reopen(cwd_i);
    return cwd_i;
  }

  if (strcmp(path, "/") == 0) {
    return dir_get_inode(dir_open_root());
  }

  // Check if path is relative or absolute.
  if (is_relative(path)){
    cur_dir = dir_reopen(t->cwd);
    if (cur_dir == NULL) return NULL;
  }
  else cur_dir = dir_open_root();
  get_dir_lock (dir_get_inode (cur_dir));
  // Iterate through path and find subdirectories.
  int status = 0;
  while (1) {
    status = get_next_part(part, &saved_path);
    // Name length was too long.
    if (status == -1) {
      release_dir_lock (dir_get_inode (cur_dir));
      dir_close(cur_dir);
      ASSERT (strcmp (path, "..") != 0);
      return NULL;
    }
    // Reached end of path successfully.
    else if (status == 0) {
      if (to_be_removed(dir_get_inode (cur_dir))) {
        release_dir_lock (dir_get_inode (cur_dir));
        dir_close (cur_dir);
        return NULL;
      }
      struct inode *again = inode_reopen (dir_get_inode (cur_dir));
      release_dir_lock (dir_get_inode (cur_dir));
      dir_close (cur_dir);
      return again;
    }
    // Got part of the path successfully.
    else {
      if (cur_dir != NULL && dir_lookup_unsynched(cur_dir, part, &next) && !to_be_removed(dir_get_inode(cur_dir))) {
        release_dir_lock (dir_get_inode (cur_dir));
        dir_close(cur_dir);
        // If next was not a directory, our next iteration will check if cur_dir was set to NULL.
        cur_dir = dir_open(next);
        get_dir_lock (dir_get_inode (cur_dir));
      }
      // Couldn't find next part of path in directory. Return NULL.
      else {
        release_dir_lock (dir_get_inode (cur_dir));
        dir_close(cur_dir);
        return NULL;
      }
    }
  }
}

/* Opens the directory the path is referring to.
   Assumes caller closes directory. Leaves dir opened. */
struct dir *get_dir_from_path(char *path) {
  struct inode *inn = get_inode_from_path (path);
  if (inn == NULL)
  {
    return NULL;
  }
  ASSERT (inode_is_dir (inn));
  ASSERT (inode_is (inn));
  return dir_open (inn);
}

/* Returns the subdirectory of the path. For example, returns "a/b/c/" on an input of "a/b/c/d" */
struct dir *get_subdir_from_path(char *path) {
  int path_len = strlen(path);
  char copy[PATH_MAX + 1];
  memcpy(copy, path, path_len);

  const char *end = copy + path_len - 1;

  while (*end == '/' && path_len != 0) {
    end--;
    path_len--;
  }
  while (*end != '/' && path_len != 0) {
    end--;
    path_len--;
  }
  copy[path_len] = '\0';
  struct dir* ret = get_dir_from_path(copy);
  return ret;
}

/* Creates a subdirectory with name "name" inside parent directory "parent".
   The subdirectory has the . and .. entries appended to it.
   This code was derived from filesys_create in filesys.c. */


bool subdir_create(char *name, struct dir *parent) {
  char new_name[NAME_MAX + 1];
  get_last_part(new_name, &name);
  block_sector_t inode_sector = 0;
  get_dir_lock (dir_get_inode (parent));
  bool success = (parent != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, 2)
                  && dir_add_unsynched (parent, new_name, inode_sector));

  if (!success){
    if (inode_sector != 0)
      free_map_release (inode_sector, 1);
    release_dir_lock (dir_get_inode (parent));
    return false;
  }
  struct inode *new = NULL;

  if (dir_lookup_unsynched(parent, new_name, &new)) {
    inode_set_dir(new);
    block_sector_t *parent_sector = get_inode_sector(dir_get_inode(parent));
    block_sector_t *new_sector = get_inode_sector(new);
    struct dir* dot_entry = dir_open(inode_open(*new_sector));
    struct dir* dotdot_entry = dir_open(inode_open(*parent_sector));
    dir_add_unsynched(dot_entry, ".", *new_sector);
    dir_add_unsynched(dot_entry, "..", *parent_sector);
    dir_close(dot_entry);
    dir_close(dotdot_entry);

    inode_close(new);
  } else {
    success = false;
  }

  release_dir_lock (dir_get_inode (parent));
  dir_close (parent);
  return success;
}

bool is_empty(struct dir* dir) {
  struct dir_entry e;
  size_t ofs;

  ASSERT (dir != NULL);
  get_dir_lock (dir_get_inode (dir));

  for (ofs = 0 * (sizeof e); inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use && !(strcmp (".", e.name) == 0) && !(strcmp ("..", e.name) == 0))
    {
      release_dir_lock (dir_get_inode (dir));
      return false;
    }
  release_dir_lock (dir_get_inode (dir));
  return true;
}

bool
dir_readdir_2 (struct dir *dir, char name[NAME_MAX + 1])
{
  //lock_acquire
  get_dir_lock (dir_get_inode (dir));
  struct dir_entry e;
  if (!inode_is (dir->inode)) return false;
  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e)
    {
      dir->pos += sizeof e;
      if (e.in_use && !(strcmp (".", e.name) == 0) && !(strcmp ("..", e.name) == 0))
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          release_dir_lock (dir_get_inode (dir));
          return true;
        }
    }
  release_dir_lock (dir_get_inode (dir));
  return false;
}

/* End Segment */
