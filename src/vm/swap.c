#include "vm/swap.h"

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

#define SWAP_FREE 0 
#define SWAP_USING 1

struct lock swap_lock;
struct block *swap_block;
struct bitmap *swap_bitmap;

void
swap_init (void)
{

  /* Get the swap space. */
  swap_block = block_get_role (BLOCK_SWAP);
  if (swap_block == NULL)
    return;

  /* Get the bitmap for swapping. */
  swap_bitmap = bitmap_create (block_size(swap_block)/SECTORS_PER_PAGE);
  if(swap_bitmap == NULL)
    return;

  /* Set the all bits of bitmap 0. */
  bitmap_set_all (swap_bitmap, SWAP_FREE);
  lock_init (&swap_lock);
}

void
swap_in(size_t used_index, void *kaddr)
{
  int i=0;
  /* If swap block or swap_map is null, do nothing. */
  if (swap_block == NULL || swap_bitmap == NULL)
    return;

  lock_acquire (&swap_lock);
  /* If the bit at USED_INDEX is 0, The block is free.
     Thus, we don't need to swap in the page.  */
  if(bitmap_test (swap_bitmap, used_index) == SWAP_FREE)
    exit (-1);

  /* The bit of the swapped block is flipped. */
  bitmap_flip (swap_bitmap, used_index);

  /* read from SWAP_BLOCK to KADDR. [Disk -> Memory] */
  for (i=0; i<SECTORS_PER_PAGE; i++)
    block_read (swap_block, used_index*SECTORS_PER_PAGE + i, 
                (uint8_t *)kaddr + i*BLOCK_SECTOR_SIZE);

  lock_release (&swap_lock);
}

size_t
swap_out (void *kaddr)
{
  int i=0;
  /* If swap block or swap_map is null, do not anything. */
  if (swap_block == NULL || swap_bitmap == NULL)
    exit(-1);

  lock_acquire (&swap_lock);
  /* Scan first zero bit of bitmap and flip it. */
  size_t idx_to_write = bitmap_scan_and_flip (swap_bitmap, 0, 1, SWAP_FREE);

  if(idx_to_write == BITMAP_ERROR)
    exit(-1);

  /* write from KADDR to SWAP_BLOCK. [Memory -> Disk]*/
  for (i=0; i<SECTORS_PER_PAGE; i++)
    block_write (swap_block, idx_to_write * SECTORS_PER_PAGE+i,
                 (uint8_t *)kaddr+i * BLOCK_SECTOR_SIZE);

  lock_release (&swap_lock);
  return idx_to_write;
}
