#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/interrupt.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define NUM_DIRECT_PTRS 12

int g_inodes_created = 0;
int g_inodes_freed = 0;

uint8_t zero_block[BLOCK_SECTOR_SIZE] = {0};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

static inline size_t
bytes_to_sector_index (off_t offest)
{
  return offest / BLOCK_SECTOR_SIZE;
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    bool extending;
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    off_t length;
    struct lock inode_lock;
    struct condition until_not_extending;
    struct condition until_no_writers;           /* No longer store Inode content. */

    uint32_t magic;
    /* Project 3 Task 3 */
    struct lock inode_dir_lock;
  };

  off_t inode_get_length (block_sector_t sector)
  {
    off_t length;
    read_buffered (fs_device, sector, &length, 0, sizeof (off_t));
    return length;
  }

  void inode_set_length (block_sector_t sector, off_t length)
  {
    write_buffered (fs_device, sector, &length, 0, sizeof (off_t));
  }

  uint32_t inode_get_is_dir (block_sector_t sector)
  {
    uint32_t is_dir;
    read_buffered (fs_device, sector, &is_dir, 4, 4 + sizeof (off_t));
    return is_dir;
  }

  void inode_set_is_dir(block_sector_t sector, uint32_t is_dir)
  {
    write_buffered (fs_device, sector, &is_dir, 4, 4 + sizeof (off_t));
  }

  uint32_t inode_get_direct_ptr (block_sector_t sector, int i)
  {
    block_sector_t tar;
    read_buffered (fs_device, sector, &tar, 8 + 4 * i, 8 + 4 * i + sizeof (block_sector_t));
    return tar;
  }

  void inode_set_direct_ptr(block_sector_t sector, int i, block_sector_t tar)
  {
    write_buffered (fs_device, sector, &tar, 8 + 4 * i, 8 + 4 * i + sizeof (block_sector_t));
  }

  block_sector_t inode_get_single_ptr (block_sector_t sector)
  {
    block_sector_t tar;
    read_buffered (fs_device, sector, &tar, 8 + 4 * NUM_DIRECT_PTRS, 8 + 4 * NUM_DIRECT_PTRS + sizeof (block_sector_t));
    return tar;
  }

  void inode_set_single_ptr(block_sector_t sector, block_sector_t tar)
  {
    write_buffered (fs_device, sector, &tar, 8 + 4 * NUM_DIRECT_PTRS, 8 + 4 * NUM_DIRECT_PTRS + sizeof (block_sector_t));
  }

  block_sector_t inode_get_double_ptr (block_sector_t sector)
  {
    block_sector_t tar;
    read_buffered (fs_device, sector, &tar, 8 + 4 * NUM_DIRECT_PTRS + 4, 8 + 4 * NUM_DIRECT_PTRS + 4 + sizeof (block_sector_t));
    return tar;
  }

  void inode_set_double_ptr(block_sector_t sector, block_sector_t tar)
  {
    write_buffered (fs_device, sector, &tar, 8 + 4 * NUM_DIRECT_PTRS + 4, 8 + 4 * NUM_DIRECT_PTRS + 4 + sizeof (block_sector_t));
  }

  void inode_set_magic(block_sector_t sector, unsigned magic)
  {
    write_buffered (fs_device, sector, &magic, 8 + 4 * NUM_DIRECT_PTRS + 8, 8 + 4 * NUM_DIRECT_PTRS + 8 + sizeof (unsigned));
  }


static bool get_sector (block_sector_t *sector)
{
  bool b = free_map_allocate (1, sector);
  if (!b) return false;
  write_buffered (fs_device, *sector, zero_block, 0, BLOCK_SECTOR_SIZE);
  return true;
}

static bool can_allocate (size_t num)
{
  block_sector_t sectors[num];
  for (size_t i = 0; i < num; i++)
  {
    if (!get_sector (&sectors[i]))
    {
      for (int j = i - 1; j > 0; j --)
      {
        free_map_release (sectors[j], 1);
      }
      return false;
    }
  }
  for (int j = num - 1; j > 0; j --)
  {
    free_map_release (sectors[j], 1);
  }
  return true;
}

static block_sector_t read_sector (block_sector_t sector, int index)
{
  ASSERT (sector);
  uint8_t buffer[sizeof(block_sector_t)];
  read_buffered (fs_device, sector, buffer, index * sizeof(int), index * sizeof(int) + sizeof(block_sector_t));
  return ((block_sector_t*) buffer)[0];
}

static void write_sector (block_sector_t sector, int index, block_sector_t good_stuff)
{
  ASSERT (sector);
  uint8_t buffer[sizeof(block_sector_t)];
  ((block_sector_t*) buffer)[0] = good_stuff;
  write_buffered (fs_device, sector, buffer, index * sizeof(int), index * sizeof(int) + sizeof(block_sector_t));
}

#define Indirect_Block (BLOCK_SECTOR_SIZE / 4)

static void install_sector (block_sector_t sector, int i)
{
  ASSERT (i < NUM_DIRECT_PTRS + BLOCK_SECTOR_SIZE / 4 * (1 + BLOCK_SECTOR_SIZE / 4));
  block_sector_t sec;
  ASSERT (get_sector (&sec));
  write_buffered (fs_device, sec, zero_block, 0, BLOCK_SECTOR_SIZE);
  if (i < NUM_DIRECT_PTRS)
  {
    inode_set_direct_ptr(sector, i, sec);
  }
  else if (i < NUM_DIRECT_PTRS + BLOCK_SECTOR_SIZE / 4)
  {
    if (inode_get_single_ptr(sector) == 0)
    {
      block_sector_t new_sector;
      ASSERT (get_sector (&new_sector));
      inode_set_single_ptr (sector, new_sector);
    }
    write_sector (inode_get_single_ptr (sector), i - NUM_DIRECT_PTRS, sec);
  }
  else
  {
    if (inode_get_double_ptr (sector) == 0)
    {
      block_sector_t new_sector;
      ASSERT (get_sector (&new_sector));
      inode_set_double_ptr (sector, new_sector);
    }
    int dab = i - NUM_DIRECT_PTRS - Indirect_Block;
    ASSERT (dab >= 0);
    block_sector_t ind_sec = read_sector (inode_get_double_ptr (sector), dab / Indirect_Block);
    if (ind_sec == 0)
    {
      ASSERT (get_sector (&ind_sec));
      write_sector (inode_get_double_ptr (sector), dab / Indirect_Block, ind_sec);
    }
    write_sector (ind_sec, dab % Indirect_Block, sec);
  }
}

static bool inode_extend (struct inode *inode, size_t sectors)
  {
    size_t from = bytes_to_sector_index (inode_get_length (inode->sector) - 1);
    if (inode_get_length (inode->sector) == 0)
    {
      from = 0;
    }
    else
    {
      from ++;
    }
    if (!can_allocate (sectors)) return false;
    for (size_t i = from; i < from + sectors; i ++)
    {
      install_sector (inode->sector, i);
    }
    return true;
  }

/*
This function is called always from inode_write, what this does is it checks if
the disk_node needs to be extended given its new length and extends accordingly
if it needs to. :^)
*/
static bool inode_extend_to_bytes (struct inode *inode, off_t new_length)
{
  size_t from = bytes_to_sector_index (inode_get_length (inode->sector) - 1);
  size_t to = bytes_to_sector_index (new_length - 1);
  if (inode_get_length (inode->sector) == 0)
  {
    if (inode_extend (inode, to + 1)) {
      inode_set_length (inode->sector, new_length);
      return true;
    }
    return false;
  }
  if (from >= to)
  {
    if (inode_get_length (inode->sector) < new_length)
    {
      inode_set_length (inode->sector, new_length);
    }
    return true;
  }
  if (inode_extend (inode, to - from)) {
      inode_set_length (inode->sector, new_length);
      return true;
  }
  return false;
}

static bool inode_extend_start (block_sector_t sector, size_t sectors)
{
  for (size_t i = 0; i < sectors; i++)
  {
    install_sector (sector, i);
  }
  return true;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  int i = bytes_to_sector_index (pos);
  ASSERT (i < NUM_DIRECT_PTRS + BLOCK_SECTOR_SIZE / 4 * (1 + BLOCK_SECTOR_SIZE / 4));
  if (pos >= inode_get_length (inode->sector)) return -1;
  ASSERT (pos < inode_get_length (inode->sector));
  block_sector_t sector;
  if (i < NUM_DIRECT_PTRS)
  {
    sector = inode_get_direct_ptr (inode->sector, i);
  }
  else if (i < NUM_DIRECT_PTRS + Indirect_Block)
  {
    ASSERT (inode_get_single_ptr (inode->sector));
    sector = read_sector (inode_get_single_ptr (inode->sector), i - NUM_DIRECT_PTRS);
  }
  else
  {
    ASSERT (inode_get_double_ptr (inode->sector));
    ASSERT (i >= NUM_DIRECT_PTRS + Indirect_Block);
    block_sector_t dab = i - NUM_DIRECT_PTRS - Indirect_Block;
    block_sector_t sec_mabel = read_sector (inode_get_double_ptr (inode->sector), dab / Indirect_Block);
    ASSERT (sec_mabel);
    sector = read_sector (sec_mabel, dab % Indirect_Block);
  }
  ASSERT (sector);
  return sector;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

struct lock open_lock;
/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  lock_init (&open_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device. And sets is_dir accordingly
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create_wild (block_sector_t sector, off_t length, bool is_dir)
{
  bool success = false;

  ASSERT (length >= 0);

  size_t sectors = bytes_to_sectors (length);
  if (can_allocate (sectors))
  {
    inode_set_length (sector, length);
    inode_set_is_dir (sector, is_dir);
    inode_set_magic (sector, INODE_MAGIC);
    success = inode_extend_start (sector, sectors);
    return success;
  }
  return success;
}

bool
inode_create (block_sector_t sector, off_t length)
{
  return inode_create_wild (sector, length, 0);
}

static void lock (struct inode *inode)
{
  ASSERT (inode->magic == INODE_MAGIC);
  lock_acquire (&(inode->inode_lock));
}

static void rel (struct inode *inode)
{
  lock_release (&(inode->inode_lock));
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  lock_acquire (&open_lock);
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode->open_cnt++;
          lock_release (&open_lock);
          return inode;
        }
    }


  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
  {
    ASSERT (inode);
    lock_release (&open_lock);
    return NULL;
  }

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_release (&open_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  inode->magic = INODE_MAGIC;
  lock_init (&(inode->inode_lock));

  /* Project 3 Task 3 */
  lock_init (&(inode->inode_dir_lock));

  g_inodes_created ++;
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  ASSERT (inode);
  if (inode == NULL)
  return NULL;
  lock (inode);
  inode->open_cnt++;
  rel (inode);
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (struct inode *inode)
{
  lock (inode);
  block_sector_t sector = inode->sector;
  rel (inode);
  return sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
struct lock ll;
bool first = true;
void
inode_close (struct inode *inode)
{
  if (first)
  {
    first = false;
    lock_init (&ll);
  }
  /* Ignore null pointer. */
  if (inode == NULL)
  {
    return;
  }
  lock_acquire (&inode->inode_lock);
  bool should_free = false;
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
  {
    /* Remove from inode list and release lock. */
    list_remove (&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed)
    {
      free_map_release (inode->sector, 1);
      for (int i = 0; i < NUM_DIRECT_PTRS; i ++)
      {
        if (inode_get_direct_ptr (inode->sector, i) == 0) break;
        free_map_release (inode_get_direct_ptr (inode->sector, i), 1);
      }
      bool clear_data (block_sector_t sector, int level)
      {
        if (sector == 0){return true;}
        if (level == 0){ ASSERT (false);}
        if (level == 1)
        {
          free_map_release (sector, 1);
          return false;
        }
        for (int i = 0; i < BLOCK_SECTOR_SIZE / 4; i ++)
        {
          if (clear_data (read_sector (sector, i), level - 1))
          {
            return true;
          }
        }
        return false;
      }
      clear_data (inode_get_single_ptr (inode->sector), 2);
      clear_data (inode_get_double_ptr (inode->sector), 3);
    }
    should_free = true;
  }
  lock_release (&inode->inode_lock);
  if (should_free)
  {
    ASSERT (inode_is(inode));
    inode->magic = -1;
    g_inodes_freed ++;
    free (inode);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  lock (inode);
  inode->removed = true;
  rel (inode);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached.
   Use the buffer cache instead of the bounce buffer. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {
  ASSERT (inode);
  lock (inode);
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      read_buffered (fs_device, sector_idx, buffer + bytes_read, sector_ofs, sector_ofs + chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  rel (inode);

  return bytes_read;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at_no_buffer (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  ASSERT (false);
  ASSERT (inode);
  lock (inode);
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;
  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);
  rel (inode);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.)
   Use the buffer cache instead of the bounce buffer.*/
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) {
  ASSERT (inode);
  lock (inode);
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  bool s = inode_extend_to_bytes (inode, offset+size);
  if (inode->deny_write_cnt)
  {
    rel (inode);
    return 0;
  }
  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      write_buffered (fs_device, sector_idx, buffer + bytes_written, sector_ofs, sector_ofs + chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  rel (inode);

  return bytes_written;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at_no_buffer (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  ASSERT (inode);
  lock (inode);
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;
  bool s = inode_extend_to_bytes (&inode, offset+size);
  if (inode->deny_write_cnt)
  {
    rel (inode);
    return 0;
  }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);
  rel (inode);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  ASSERT (inode);
  lock (inode);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  rel (inode);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock (inode);
  inode->deny_write_cnt--;
  rel (inode);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  ASSERT (inode);
  return inode_get_length (inode->sector);
}

/* Project 3 Task 3 */

bool inode_is_dir(const struct inode *inode) {
  ASSERT (inode != NULL);
  if (inode == NULL) return false;
  lock (inode);
  if (inode_get_is_dir (inode->sector) == 1)
  {
    rel (inode);
    return true;
  }
  rel (inode);
  return false;
}

void get_dir_lock(const struct inode *inode) {
  ASSERT (inode != NULL);
  lock (inode);
  lock_acquire(&(inode->inode_dir_lock));
  rel (inode);
}

void release_dir_lock(const struct inode *inode) {
  ASSERT (inode != NULL);
  lock (inode);
  lock_release(&(inode->inode_dir_lock));
  rel (inode);
}

void inode_set_dir(struct inode *inode) {
  inode_set_is_dir (inode->sector, 1);
}

block_sector_t *get_inode_sector(const struct inode* inode) {
  return &inode->sector;
}

block_sector_t
o_inumber (struct inode *inode) {
  ASSERT (inode != NULL);
  return inode_get_inumber(inode);
}

bool to_be_removed (struct inode* inode) {
  bool removed;
  lock (inode);
  if (inode->removed) removed = true;
  else removed = false;
  rel(inode);
  return removed;
}

bool inode_is (struct inode* inode)
{
  return inode->magic == INODE_MAGIC;
}

int inode_cnt (struct inode* inode)
{
  ASSERT (inode);
  ASSERT (inode->magic == INODE_MAGIC);
  return inode->open_cnt;
}
