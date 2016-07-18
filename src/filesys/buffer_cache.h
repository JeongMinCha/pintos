#ifndef FILESYS_BUFFER_H
#define FILESYS_BUFFER_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"

#define REFER_BIT 0x1  /* 1 = Not victim, 0 = Victim */

struct buffer_head
  {
    bool dirty;
    bool in_use;
    block_sector_t sector;
    uint32_t clock_bit;
    struct lock lock;
    void* data;
  };

bool bc_read (block_sector_t sector_idx, void* buffer, 
              off_t bytes_read, int chunk_size, int sector_ofs);
bool bc_write (block_sector_t sector_idx, void* buffer, 
               off_t bytes_written, int chunk_size, int sector_ofs);
struct buffer_head* bc_lookup (block_sector_t sector);
struct buffer_head* bc_select_victim (void);
void bc_flush_entry (struct buffer_head *p_flush_entry);
void bc_flush_all_entries (void);
void bc_init (void);
void bc_term (void);

#endif /* filesys/buffer_cahce.h */

