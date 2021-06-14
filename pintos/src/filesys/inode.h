#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t);
bool inode_create_wild (block_sector_t sector, off_t length, bool is_dir);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_read_at_no_buffer (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
off_t inode_write_at_no_buffer (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

/* Project 3 Task 3 */
bool inode_is_dir(const struct inode *);
void get_dir_lock(const struct inode *);
void release_dir_lock(const struct inode *);
void inode_set_dir(struct inode *);
block_sector_t *get_inode_sector(const struct inode*);
uint32_t o_inumber (struct inode *); //Proj 3 added
bool to_be_removed (struct inode *);
bool inode_is (struct inode* inode);
int inode_cnt (struct inode* inode);

#endif /* filesys/inode.h */
