#include "filesys/buffer_cache.h"
#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/*
struct buffer_head
  {
    struct inode* inode;
    bool dirty;
    bool in_use;
    block_sector_t sector;
    uint32_t clock_bit;
    struct lock lock;
    void* data
  }
*/

/* 64 * 512bytes = 32 kbytes. */
#define BUFFER_CACHE_ENTRY_NB 64

/* Points the buffer cache memory space. */
void *p_buffer_cache;

/* The array of buffer cache entries. */
struct buffer_head buffer_head[BUFFER_CACHE_ENTRY_NB];

/* We use the clock algorithm for selecting the victime 
   buffer cache entry. It is used for the index for the victim. */
int clock_hand;

void bc_init (void)
{
  int i;
  /* Allocate buffer cache in Memory 
     P_BUFFER_CACHE points the buffer cache space. */
  p_buffer_cache = malloc (BLOCK_SECTOR_SIZE * BUFFER_CACHE_ENTRY_NB);

  /* Initalize all entries in BUFFER_HEAD. */
  for (i=0; i<BUFFER_CACHE_ENTRY_NB; i++)
  {
    lock_init(&buffer_head[i].lock);
    buffer_head[i].in_use = false;
    buffer_head[i].dirty = false;
    buffer_head[i].clock_bit = 0;
    buffer_head[i].data = p_buffer_cache + i * BLOCK_SECTOR_SIZE;
  }
  clock_hand = 0;
}

void bc_term(void)
{
  /* Flush all entries in BUFFER_HEAD to the disk. */
  bc_flush_all_entries();
  /* Free the allocation for buffer cache. */
  free(p_buffer_cache);
}

/* Read the data from the buffer cache to BUFFER. */
bool bc_read (block_sector_t sector_idx, void* buffer, 
              off_t bytes_read, int chunk_size, int sector_ofs)
{
  bool success = false;
  struct buffer_head *cache_p;

  /* Search the buffer cache entry with SECTOR_IDX
     in BUFFER_HEAD. */
  cache_p = bc_lookup (sector_idx);

  /* If we cannot find the entry, use BC_SELECT_VICTIM. */
  if(!cache_p)
  {
    cache_p = bc_select_victim();
    cache_p->in_use = true;
    cache_p->sector = sector_idx;
    
    /* Read the sector SECTOR_IDX from the disk
       into the buffer cache. */
    block_read (fs_device, sector_idx, cache_p->data);
  }

  /* Copy the buffer cache data to BUFFER. */
  memcpy(buffer + bytes_read, 
         cache_p->data + sector_ofs, chunk_size);
  /* Set the CLOCK_BIT of the buffer cache. */
  cache_p->clock_bit |= REFER_BIT;

  success = true;
  return success;
}

/* Write the BUFFER data to the buffer cache. */
bool bc_write (block_sector_t sector_idx, void* buffer, 
               off_t bytes_written, int chunk_size, int sector_ofs)
{
  bool success = false;
  struct buffer_head *cache_p;
  
  /* Search the buffer cache entry with SECTOR_IDX
     in BUFFER_HEAD. */
  cache_p = bc_lookup (sector_idx);

  /* There are no empty entry, select the victim and 
     flush it to the disk. */
  if(!cache_p)
  {
    cache_p = bc_select_victim();
    cache_p->in_use = true;
    cache_p->sector = sector_idx;

    /* Read the sector SECTOR_IDX from the disk
       into the buffer cache. */
    block_read (fs_device, sector_idx, cache_p->data);
  }

  /* Copy BUFFER to the buffer cache. */
  memcpy (cache_p->data + sector_ofs,
          buffer + bytes_written, chunk_size);
  /* Set the CLOCK_BIT of the buffer cache. */
  cache_p->clock_bit |= REFER_BIT;
  /* Set it dirty because we write some data to it. */
  cache_p->dirty = true;

  success = true;
  return success;
}

/* Using the clock algorithm, select the victim buffer caache entry. 
   If needed, we flush the victim entry and return the victim entry. */
struct buffer_head* bc_select_victim (void) 
{
  while (true)
  { 
    /* If a buffer cache is not being used, select it the victim. */
    if(!buffer_head[clock_hand].in_use)
      return &buffer_head[clock_hand];

    /* Check CLOCK_BIT of each buffer cache entry. If the bit is 0, */
    if((buffer_head[clock_hand].clock_bit & REFER_BIT) == 0)
    {
      /* If the buffer cache entry is dirty, */
      if(buffer_head[clock_hand].dirty)
      { 
        /* flush it to the disk. */
        block_write(fs_device, buffer_head[clock_hand].sector, 
                    buffer_head[clock_hand].data);
        /* After flushing, the entry is not dirty. */
        buffer_head[clock_hand].dirty = false;
      }
      /* Set it not being used. */
      buffer_head[clock_hand].in_use = false;

      /* returns the victim entry. */
      return &buffer_head[clock_hand];
    }
    else 
    {
      /* If CLOCK_BIT is 1, set it 0. */
      buffer_head[clock_hand].clock_bit = ~(uint32_t) REFER_BIT;
    }
    clock_hand = (clock_hand + 1) % BUFFER_CACHE_ENTRY_NB;
  }

  return NULL;
}

struct buffer_head* bc_lookup (block_sector_t sector)
{
  int i;

  for (i=0; i<BUFFER_CACHE_ENTRY_NB; i++)
  {
    /* Search the entry at SECTOR. */
    if (buffer_head[i].in_use && buffer_head[i].sector == sector)
      return &buffer_head[i];
  }

  return NULL; 
}

void bc_flush_entry (struct buffer_head *p_flush_entry)
{
  lock_acquire(&p_flush_entry->lock); 
  /* flush the data of P_FLUSH_ENTRY to the disk. */
  block_write (fs_device, p_flush_entry->sector, p_flush_entry->data);
  /* After flushed, the entry is not dirty. */
  p_flush_entry->dirty = false;
  lock_release(&p_flush_entry->lock); 
}


void bc_flush_all_entries (void)
{
  int i;
  for (i=0; i<BUFFER_CACHE_ENTRY_NB; i++)
  {
    /* Check all dirty entries in BUFFER_HEAD. */
    if (buffer_head[i].in_use && buffer_head[i].dirty)
    {
      /* flush all entries to the disk. */ 
      block_write (fs_device, buffer_head[i].sector, buffer_head[i].data);
      /* After flushed, the entry is not dirty. */
      buffer_head[i].dirty = false;
    }
  }

}
