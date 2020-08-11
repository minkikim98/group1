#include "devices/block.h"
#include <list.h>
#include <string.h>
#include <stdio.h>
#include "devices/ide.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/interrupt.h"

/* A buffer cache entry struct. */
struct buffer_entry {
	block_sector_t buffered_sector;
	struct block *sector_block;
	char buffer[BLOCK_SECTOR_SIZE];
	int use_bit;
	int dirty_bit;
	struct lock *sector_lock;
};

/* A buffer cache. */
struct buffer_entry *buffer_cache[64];

/* Clock hand for the clock algorithm. */
int clock_hand;

/* Lock to add and evict buffer entry. */
struct lock *buffer_cache_lock;

/* Semaphore to indicate the number of occupied buffer entries. */
struct semaphore *active_sema;

/* Condition variable to indicate there is at least one inactive entry. */
struct condition *inactive_entry;

/* Lock for inactive_entry. */
struct lock *inactive_lock;

/* A block device. */
struct block
  {
    struct list_elem list_elem;         /* Element in all_blocks. */

    char name[16];                      /* Block device name. */
    enum block_type type;                /* Type of block device. */
    block_sector_t size;                 /* Size in sectors. */

    const struct block_operations *ops;  /* Driver operations. */
    void *aux;                          /* Extra data owned by driver. */

    unsigned long long read_cnt;        /* Number of sectors read. */
    unsigned long long write_cnt;       /* Number of sectors written. */
  };

/* List of all block devices. */
static struct list all_blocks = LIST_INITIALIZER (all_blocks);

/* The block block assigned to each Pintos role. */
static struct block *block_by_role[BLOCK_ROLE_CNT];

static struct block *list_elem_to_block (struct list_elem *);

/* Returns a human-readable name for the given block device
   TYPE. */
const char *
block_type_name (enum block_type type)
{
  static const char *block_type_names[BLOCK_CNT] =
    {
      "kernel",
      "filesys",
      "scratch",
      "swap",
      "raw",
      "foreign",
    };

  ASSERT (type < BLOCK_CNT);
  return block_type_names[type];
}

/* Get the offset of block_sector_t in the buffer_cache.
Acquire a lock for the corresponding entry and return the offset.
Return -1 if it's not inside the buffer cache.
We acquire buffer_cache_lock to make sure no eviction will happen in this process. */
int acquire_buffer_entry_lock(block_sector_t sector) {
	lock_acquire(buffer_cache_lock);
	int i;
	for (i = 0; i < 64; i ++) {
		if (buffer_cache[i]->buffered_sector == sector) {
			lock_acquire(buffer_cache[i]->sector_lock);
			buffer_cache[i]->use_bit = 1;
			lock_release(buffer_cache_lock);
			return i;
		}
	}
	lock_release(buffer_cache_lock);
	return -1;
}

/* Check if a sector is already cached.
The caller has to acquire buffer_cache_lock first. */
bool check_sector_cached(block_sector_t sector) {
	ASSERT (lock_held_by_current_thread(buffer_cache_lock));
	int i;
	for (i = 0; i < 64; i ++) {
		if (buffer_cache[i]->buffered_sector == sector) {
			return true;
		}
	}
	lock_release(buffer_cache_lock);
	return false;
}

/* Check if the corresponding buffer entry is still present.
1. The buffer entry can not be NULL.
2. The sector number must stay the same.
3. The block entry should be the same one that the thread acquires the lock on.
The caller must have acquired the lock on the buffer entry if present.
As a result, we don't need to acquire buffer_cache_lock because if it's still
present, it won't be evicted. */
bool check_buffer_presence(block_sector_t sector, int offset) {
	ASSERT (lock_held_by_current_thread(buffer_cache_lock));
	if (buffer_cache[offset] == NULL) {
		return false;
	}
	if (buffer_cache[offset]->buffered_sector != sector) {
		return false;
	}
	if (!lock_held_by_current_thread(buffer_cache[offset]->sector_lock)) {
		return false;
	}
	return true;
}

/* Evicts buffer entry from buffer cache. */
void buffer_evict(int offset) {
  ASSERT (lock_held_by_current_thread(buffer_cache_lock));

  struct buffer_entry *cur = buffer_cache[offset];
	ASSERT (lock_held_by_current_thread(cur->sector_lock));

  block_write(cur->sector_block, cur->buffered_sector, cur->buffer);
  buffer_cache[offset] = NULL;
	lock_release(cur->sector_lock);
  free(cur);
}

/* Initialize buffer cache. */
void init_buffer_cache (void) {
  int i = 0;
  for (; i < 64; i ++) {
    buffer_cache[i] = NULL;
  }
  clock_hand = 0;
  lock_init(buffer_cache_lock);
  sema_init(active_sema, 64);
	lock_init(inactive_lock);
	cond_init(inactive_entry);
}

/* Flush buffer cache. */
void flush_buffer_cache (void) {
  lock_acquire(buffer_cache_lock);
  int i = 0;
  for (; i < 64; i ++) {
    if (buffer_cache[i] != NULL) {
			lock_acquire(buffer_cache[i] -> sector_lock);
      buffer_evict(i);
    }
  }
  lock_release(buffer_cache_lock);
}

/* Evict a buffer entry with clock algorithm.
The caller needs to make sure that there is at least 1
empty or inactive buffer entry. */
int clock_algorithm_evict(void) {
	ASSERT (lock_held_by_current_thread(buffer_cache_lock));
	int offset;
	while (true) {
		if (buffer_cache[clock_hand] == NULL) {
			offset = clock_hand;
			clock_hand = (clock_hand + 1) % 64;
			return offset;
		} else {
			if (lock_try_acquire(buffer_cache[clock_hand]->sector_lock)) {
				if (buffer_cache[clock_hand]->use_bit == 1) {
					buffer_cache[clock_hand]->use_bit = 0;
					clock_hand = (clock_hand + 1) % 64;
					lock_release(buffer_cache[clock_hand]->sector_lock);
				} else {
					buffer_evict(clock_hand);
					offset = clock_hand;
					clock_hand = (clock_hand + 1) % 64;
					return offset;
				}
			} else {
				clock_hand = (clock_hand + 1) % 64;
			}
		}
	}
}

/* Read from cache_buffer to input_buffer, from start to end.
src points to a sector. */
void bounded_read(char *input_buffer, char *cache_buffer, off_t start, off_t end) {
	cache_buffer = cache_buffer + start;
	memcpy(input_buffer, cache_buffer, end - start);
}

/* Write from src to dest, from start to end.
dest points to a sector. */
void bounded_write(char *input_buffer, char *cache_buffer, off_t start, off_t end) {
	cache_buffer = cache_buffer + start;
	memcpy(cache_buffer, input_buffer, end - start);
}

/* Read buffered content from buffer cache.
If not buffered, call read_not_buffered. */
void read_buffered(struct block * block, block_sector_t sector , void * buffer, off_t start, off_t end) {
	int offset = acquire_buffer_entry_lock(sector);
	if (offset == -1) {
		return read_not_buffered(block, sector , buffer, start, end);
	}
	struct buffer_entry *cur = buffer_cache[offset];
	enum intr_level old_level;
	old_level = intr_disable (); // We disable interrupt because if we can't sema down we need to release the sector_lock atomically.
	while (!sema_try_down(active_sema)) {
		lock_release(cur->sector_lock);
		intr_set_level (old_level);

		lock_acquire(inactive_lock);
		cond_wait(inactive_entry, inactive_lock);
		lock_release(inactive_lock);

		offset = acquire_buffer_entry_lock(sector); // When we are waiting, the previous buffer entry could be evicted.
		if (offset == -1 || !check_buffer_presence(sector, offset)) {
			return read_not_buffered(block, sector , buffer, start, end);
		}

		old_level = intr_disable ();
	}
	intr_set_level (old_level); // We have acquire the lock and performed sema down to mark an active buffer entry.
	bounded_read((char *) buffer, buffer_cache[offset]->buffer, start, end);

	lock_release(buffer_cache[offset]->sector_lock);
	sema_up(active_sema);
	lock_acquire(inactive_lock);
	cond_wait(inactive_entry, inactive_lock); // Signal all waiters that there is at least an inactive entry.
	lock_release(inactive_lock);
}

/* Read from disk, load into buffer cache, and load into buffer. */
void read_not_buffered(struct block * block , block_sector_t sector , void * buffer, off_t start, off_t end) {
	lock_acquire(buffer_cache_lock);
	if (check_sector_cached(sector)) {
		return read_buffered(block, sector , buffer, start, end);
	}
	enum intr_level old_level;
	old_level = intr_disable ();
	while (!sema_try_down(active_sema)) {
		lock_release(buffer_cache_lock);

		intr_set_level (old_level);

		lock_acquire(inactive_lock);
		cond_wait(inactive_entry, inactive_lock);
		lock_release(inactive_lock);

		lock_acquire(buffer_cache_lock);
		if (check_sector_cached(sector)) {
			return read_buffered(block, sector , buffer, start, end);
		}

		old_level = intr_disable ();
	}
	intr_set_level (old_level);
	struct buffer_entry *cur = malloc(sizeof(struct buffer_entry));
	cur->buffered_sector = sector;
	cur->sector_block = block;
	cur->use_bit = 1;
	cur->dirty_bit = 0;
	lock_init(cur->sector_lock);
	block_read(block, sector, cur->buffer);

	int offset = clock_algorithm_evict();
	ASSERT (buffer_cache[offset] == NULL);

	bounded_read((char *) buffer, cur->buffer, start, end);

	buffer_cache[offset] = cur;

	lock_release(buffer_cache_lock);
	sema_up(active_sema);
	lock_acquire(inactive_lock);
	cond_wait(inactive_entry, inactive_lock); // Signal all waiters that there is at least an inactive entry.
	lock_release(inactive_lock);
}


/* Write from buffered content to buffer cache.
If not buffered, call write_not_buffered. */
void write_buffered(struct block * block, block_sector_t sector , void * buffer, off_t start, off_t end) {
	int offset = acquire_buffer_entry_lock(sector);
	if (offset == -1) {
		return write_not_buffered(block, sector , buffer, start, end);
	}
	struct buffer_entry *cur = buffer_cache[offset];
	enum intr_level old_level;
	old_level = intr_disable (); // We disable interrupt because if we can't sema down we need to release the sector_lock atomically.
	while (!sema_try_down(active_sema)) {
		lock_release(cur->sector_lock);
		intr_set_level (old_level);

		lock_acquire(inactive_lock);
		cond_wait(inactive_entry, inactive_lock);
		lock_release(inactive_lock);

		offset = acquire_buffer_entry_lock(sector); // When we are waiting, the previous buffer entry could be evicted.
		if (offset == -1 || !check_buffer_presence(sector, offset)) {
			return write_not_buffered(block, sector , buffer, start, end);
		}

		old_level = intr_disable ();
	}
	intr_set_level (old_level); // We have acquire the lock and performed sema down to mark an active buffer entry.
	bounded_write((char *) buffer, buffer_cache[offset]->buffer, start, end);
	buffer_cache[offset]->dirty_bit = 1;

	lock_release(buffer_cache[offset]->sector_lock);
	sema_up(active_sema);
	lock_acquire(inactive_lock);
	cond_wait(inactive_entry, inactive_lock); // Signal all waiters that there is at least an inactive entry.
	lock_release(inactive_lock);
}

/* Read from disk, load into buffer cache, and write from buffer to buffer entry. */
void write_not_buffered(struct block * block , block_sector_t sector , void * buffer, off_t start, off_t end) {
	lock_acquire(buffer_cache_lock);
	if (check_sector_cached(sector)) {
		return write_buffered(block, sector , buffer, start, end);
	}
	enum intr_level old_level;
	old_level = intr_disable ();
	while (!sema_try_down(active_sema)) {
		lock_release(buffer_cache_lock);

		intr_set_level (old_level);

		lock_acquire(inactive_lock);
		cond_wait(inactive_entry, inactive_lock);
		lock_release(inactive_lock);

		lock_acquire(buffer_cache_lock);
		if (check_sector_cached(sector)) {
			return write_buffered(block, sector , buffer, start, end);
		}

		old_level = intr_disable ();
	}
	intr_set_level (old_level);
	struct buffer_entry *cur = malloc(sizeof(struct buffer_entry));
	cur->buffered_sector = sector;
	cur->sector_block = block;
	cur->use_bit = 1;
	cur->dirty_bit = 1;
	lock_init(cur->sector_lock);
	block_read(block, sector, cur->buffer);

	int offset = clock_algorithm_evict();
	ASSERT (buffer_cache[offset] == NULL);

	bounded_write(buffer, cur->buffer, start, end);

	buffer_cache[offset] = cur;

	lock_release(buffer_cache_lock);
	sema_up(active_sema);
	lock_acquire(inactive_lock);
	cond_wait(inactive_entry, inactive_lock); // Signal all waiters that there is at least an inactive entry.
	lock_release(inactive_lock);
}


/* Returns the block device fulfilling the given ROLE, or a null
   pointer if no block device has been assigned that role. */
struct block *
block_get_role (enum block_type role)
{
  ASSERT (role < BLOCK_ROLE_CNT);
  return block_by_role[role];
}

/* Assigns BLOCK the given ROLE. */
void
block_set_role (enum block_type role, struct block *block)
{
  ASSERT (role < BLOCK_ROLE_CNT);
  block_by_role[role] = block;
}

/* Returns the first block device in kernel probe order, or a
   null pointer if no block devices are registered. */
struct block *
block_first (void)
{
  return list_elem_to_block (list_begin (&all_blocks));
}

/* Returns the block device following BLOCK in kernel probe
   order, or a null pointer if BLOCK is the last block device. */
struct block *
block_next (struct block *block)
{
  return list_elem_to_block (list_next (&block->list_elem));
}

/* Returns the block device with the given NAME, or a null
   pointer if no block device has that name. */
struct block *
block_get_by_name (const char *name)
{
  struct list_elem *e;

  for (e = list_begin (&all_blocks); e != list_end (&all_blocks);
       e = list_next (e))
    {
      struct block *block = list_entry (e, struct block, list_elem);
      if (!strcmp (name, block->name))
        return block;
    }

  return NULL;
}

/* Verifies that SECTOR is a valid offset within BLOCK.
   Panics if not. */
static void
check_sector (struct block *block, block_sector_t sector)
{
  if (sector >= block->size)
    {
      /* We do not use ASSERT because we want to panic here
         regardless of whether NDEBUG is defined. */
      PANIC ("Access past end of device %s (sector=%"PRDSNu", "
             "size=%"PRDSNu")\n", block_name (block), sector, block->size);
    }
}

/* Reads sector SECTOR from BLOCK into BUFFER, which must
   have room for BLOCK_SECTOR_SIZE bytes.
   Internally synchronizes accesses to block devices, so external
   per-block device locking is unneeded. */
void
block_read (struct block *block, block_sector_t sector, void *buffer)
{
  check_sector (block, sector);
  block->ops->read (block->aux, sector, buffer);
  block->read_cnt++;
}

/* Write sector SECTOR to BLOCK from BUFFER, which must contain
   BLOCK_SECTOR_SIZE bytes.  Returns after the block device has
   acknowledged receiving the data.
   Internally synchronizes accesses to block devices, so external
   per-block device locking is unneeded. */
void
block_write (struct block *block, block_sector_t sector, const void *buffer)
{
  check_sector (block, sector);
  ASSERT (block->type != BLOCK_FOREIGN);
  block->ops->write (block->aux, sector, buffer);
  block->write_cnt++;
}

/* Returns the number of sectors in BLOCK. */
block_sector_t
block_size (struct block *block)
{
  return block->size;
}

/* Returns BLOCK's name (e.g. "hda"). */
const char *
block_name (struct block *block)
{
  return block->name;
}

/* Returns BLOCK's type. */
enum block_type
block_type (struct block *block)
{
  return block->type;
}

/* Prints statistics for each block device used for a Pintos role. */
void
block_print_stats (void)
{
  int i;

  for (i = 0; i < BLOCK_ROLE_CNT; i++)
    {
      struct block *block = block_by_role[i];
      if (block != NULL)
        {
          printf ("%s (%s): %llu reads, %llu writes\n",
                  block->name, block_type_name (block->type),
                  block->read_cnt, block->write_cnt);
        }
    }
}

/* Registers a new block device with the given NAME.  If
   EXTRA_INFO is non-null, it is printed as part of a user
   message.  The block device's SIZE in sectors and its TYPE must
   be provided, as well as the it operation functions OPS, which
   will be passed AUX in each function call. */
struct block *
block_register (const char *name, enum block_type type,
                const char *extra_info, block_sector_t size,
                const struct block_operations *ops, void *aux)
{
  struct block *block = malloc (sizeof *block);
  if (block == NULL)
    PANIC ("Failed to allocate memory for block device descriptor");

  list_push_back (&all_blocks, &block->list_elem);
  strlcpy (block->name, name, sizeof block->name);
  block->type = type;
  block->size = size;
  block->ops = ops;
  block->aux = aux;
  block->read_cnt = 0;
  block->write_cnt = 0;

  printf ("%s: %'"PRDSNu" sectors (", block->name, block->size);
  print_human_readable_size ((uint64_t) block->size * BLOCK_SECTOR_SIZE);
  printf (")");
  if (extra_info != NULL)
    printf (", %s", extra_info);
  printf ("\n");

  return block;
}

/* Returns the block device corresponding to LIST_ELEM, or a null
   pointer if LIST_ELEM is the list end of all_blocks. */
static struct block *
list_elem_to_block (struct list_elem *list_elem)
{
  return (list_elem != list_end (&all_blocks)
          ? list_entry (list_elem, struct block, list_elem)
          : NULL);
}
