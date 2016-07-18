#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;
struct inode_disk;

void inode_init (void);
bool inode_create (block_sector_t, off_t, uint32_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

static bool get_disk_inode(const struct inode *inode,
                           struct inode_disk * inode_disk);

static inline off_t map_table_offset(int index);

static void free_inode_sectors (struct inode_disk *);

bool inode_update_file_length(struct inode_disk *,
                              off_t start_pos, off_t end_pos);

bool inode_is_dir (const struct inode *);
int inode_get_open_cnt(struct inode *);
bool inode_is_rm (const struct inode *inode);

#endif /* filesys/inode.h */
