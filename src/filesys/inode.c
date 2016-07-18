#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/buffer_cache.h"
#include "threads/malloc.h"
/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_BLOCK_ENTRIES 123
#define INDIRECT_BLOCK_ENTRIES 128
enum direct_t
  {
    NORMAL_DIRECT,
    INDIRECT,
    DOUBLE_INDIRECT,
    OUT_LIMIT
  };

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t is_dir;                           /* file = 0, directory = 1 */
    block_sector_t direct_map_table[DIRECT_BLOCK_ENTRIES];
    block_sector_t indirect_block_sec;               /* First data sector. */
    block_sector_t double_indirect_block_sec;        /* First data sector. */
  };

struct sector_location
  {
    enum direct_t directness;
	off_t index1;
    off_t index2;
  };

struct inode_indirect_block
  {
    block_sector_t map_table[INDIRECT_BLOCK_ENTRIES];
  };



/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock extend_lock;            /* semaphore lock */
  };

static void 
locate_byte (off_t pos, struct sector_location *sec_loc)
{
  off_t pos_sector = pos / BLOCK_SECTOR_SIZE;

  /* Direct case. */
  if (pos_sector < DIRECT_BLOCK_ENTRIES)
  {
    sec_loc->directness = NORMAL_DIRECT;
    sec_loc->index1 = pos_sector;
  }
  /* Indirect case. */
  else if(pos_sector<(off_t)(DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES))
  {
    sec_loc->directness = INDIRECT;
    sec_loc->index1 = pos_sector - DIRECT_BLOCK_ENTRIES;
  }
  /* Double indirect case. */
  else if (pos_sector <(off_t)(DIRECT_BLOCK_ENTRIES + 
                       INDIRECT_BLOCK_ENTRIES * (INDIRECT_BLOCK_ENTRIES + 1)))
  {  
    sec_loc->directness = DOUBLE_INDIRECT;
    pos_sector = pos_sector - DIRECT_BLOCK_ENTRIES - INDIRECT_BLOCK_ENTRIES;
    sec_loc->index1 = pos_sector / INDIRECT_BLOCK_ENTRIES;
    sec_loc->index2 = pos_sector%INDIRECT_BLOCK_ENTRIES;
  }
  /* If wrong file offset, */
  else
    sec_loc->directness = OUT_LIMIT;
}

/* Update the new allocated disk block number at INODE_DISK. */
static bool
register_sector (struct inode_disk *inode_disk, block_sector_t new_sector, 
                 struct sector_location sec_loc)
{
  struct inode_indirect_block *new_block;

  switch (sec_loc.directness)
  {
  case NORMAL_DIRECT:
    /* Update the new disk block number at INODE_DISK. */
    inode_disk->direct_map_table[sec_loc.index1] = new_sector;
    break;
  case INDIRECT:
    new_block = malloc (BLOCK_SECTOR_SIZE);
    if (new_block == NULL)
      return false;
    if(bc_read(inode_disk->indirect_block_sec, 
       (void*)new_block, 0, BLOCK_SECTOR_SIZE, 0))
    {
      /* Save the new disk block number in the index block. */
      new_block->map_table[sec_loc.index1] = new_sector;
      /* Write the index block to buffer cache. */
      bc_write (inode_disk->indirect_block_sec, (void*)new_block,
                0, BLOCK_SECTOR_SIZE, 0);
    }
    else
      return false;
    break;
  case DOUBLE_INDIRECT :
    new_block = malloc (BLOCK_SECTOR_SIZE);
      if (new_block == NULL)
        return false;
    if(bc_read(inode_disk->double_indirect_block_sec, 
       (void*)new_block, 0, BLOCK_SECTOR_SIZE, 0))
    {
      struct inode_indirect_block *new_block2 = malloc (BLOCK_SECTOR_SIZE);
      if (new_block2 == NULL)
        return false;

      /* Save the disk block number in the second order index block, */
      if(bc_read(new_block->map_table[sec_loc.index1], 
                 (void*)new_block2, 0, BLOCK_SECTOR_SIZE, 0))
      {
        new_block2->map_table[sec_loc.index2] = new_sector;
      }
      /* And write each index block to buffer cache. */ 
      bc_write (new_block->map_table[sec_loc.index1], (void*)new_block2,
                0, BLOCK_SECTOR_SIZE, 0);
    }
    else
      return false;
    break;
    
  default:
    return false;
  }

  free(new_block);
  return true;
}

static block_sector_t
byte_to_sector (const struct inode_disk *inode_disk, off_t pos)
{
  /* Disk block number to return. */
  block_sector_t result_sec;
  if (pos < inode_disk->length)
  {
    struct inode_indirect_block *ind_block;
    struct sector_location sec_loc;

    /* calculate the offset of the index block. */
    locate_byte(pos, &sec_loc); 
 
    switch (sec_loc.directness)
    {
    /* Direct case */
    case NORMAL_DIRECT:
      /* get the disk block number from DIRECT_MAP_TABLE. */
      result_sec = inode_disk->direct_map_table[sec_loc.index1];
      break;
    /* Indirect case */
    case INDIRECT:
      ind_block = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
      if (ind_block)
      {
        /* read the index block from buffer cache. */
        /* check the disk block number in the index block. */
        if(bc_read(inode_disk->indirect_block_sec, 
                   (void*)ind_block, 0, BLOCK_SECTOR_SIZE, 0))
        {
          result_sec = ind_block->map_table[sec_loc.index1];
        }
        else
          result_sec = 0;
      }
      else
        result_sec = 0;
      free (ind_block);
      break;
    /* Double indirect case */
    case DOUBLE_INDIRECT:
      ind_block = (struct inode_indirect_block *) malloc(BLOCK_SECTOR_SIZE);
      if (ind_block)
      {
        /* read the first order index block from buffer cache. */
        if(bc_read(inode_disk->double_indirect_block_sec, 
                   (void*)ind_block, 0, BLOCK_SECTOR_SIZE, 0))
        {
          result_sec = ind_block->map_table[sec_loc.index1];
          /* read the second order index block from buffer cache. */
          /* check the disk block number in the second order index block. */
          if(bc_read(result_sec, (void*)ind_block, 
                     0, BLOCK_SECTOR_SIZE, 0))
          {
            result_sec = ind_block->map_table[sec_loc.index2];
          }
        }
      }
      else
        result_sec = 0;
      break;
    }
  }   
  return result_sec;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, uint32_t is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);
  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);
  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    /* Set dir type */
    disk_inode->is_dir = is_dir;
 
   if(length > 0)
    {
      /* allocate the disk block by calling INODE_UPDATE_FILE_LENGTH. */
      inode_update_file_length(disk_inode, 0, length);
    }
    /* write the on-disk inode into buffer cache. */
    if(bc_write (sector, (void*)disk_inode, 0, BLOCK_SECTOR_SIZE, 0))
    {
      success = true;
    }
    /* free DISK_INODE. */
    free (disk_inode);
  }

  return success;
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
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_init(&inode->extend_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
//  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Read INODE from buffer cache and give it.*/
static bool
get_disk_inode(const struct inode *inode,
               struct inode_disk *inode_disk)
{
  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *inode_disk == BLOCK_SECTOR_SIZE);

  /* Read the on-disk inode at INODE->SECTOR from buffer cache, 
     and save it to INODE_DISK. */
  if(bc_read(inode->sector, (void*)inode_disk, 0, BLOCK_SECTOR_SIZE, 0))
    return true;
  else 
    return false;
}

static inline off_t map_table_offset (int index)
{
  /* return the offset value converted in bytes. */
  return sizeof(uint32_t)*index;
}

bool
inode_update_file_length(struct inode_disk* inode_disk, 
                         off_t start_pos, off_t end_pos)
{
  struct sector_location sec_loc;
  block_sector_t sector_idx;
  char *zeroes = calloc(BLOCK_SECTOR_SIZE, sizeof(char));
  off_t size = end_pos - start_pos;

  struct inode_indirect_block *ind_block = NULL;

  off_t offset = start_pos;

  /* Allocate the new disk block for each block. */
  while(size > 0)
  {
    /* The offset in the disk block. */
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, minimum of the two. */
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int inode_left = inode_disk->length - offset; 
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    /* If the block offset > 0, it's already allocated. */
    if(sector_ofs > 0){
      /* Do nothing. */
    }
    /* If the block offset is 0, */
    else
    {
      /* Allocate the new disk block. */
      if(free_map_allocate(1, &sector_idx))
      {
        /* Update the disk block number allocated to INODE_DISK. */
        locate_byte(offset, &sec_loc);
        /* Indirect case. */
        if(sec_loc.directness == INDIRECT && sec_loc.index1 == 0)
        {
          ind_block = malloc (BLOCK_SECTOR_SIZE);
          if(!free_map_allocate(1, &inode_disk->indirect_block_sec)&&
             ind_block == NULL)
          {
            free(ind_block);
            free(zeroes);
            return false;
          }
          /* Initialize the new disk block 0. */
          bc_write (inode_disk->indirect_block_sec, (void*)ind_block,
                    0, BLOCK_SECTOR_SIZE, 0);
          free(ind_block);
        }
        /* double indirect case */
        else if(sec_loc.directness == DOUBLE_INDIRECT && sec_loc.index2 == 0)
        {
          if(sec_loc.index1 == 0)
          {
            ind_block = malloc (BLOCK_SECTOR_SIZE);
            if(!free_map_allocate(1, &inode_disk->double_indirect_block_sec)&&
               ind_block == NULL)
            {
              free(ind_block);
              free(zeroes);
              return false;
            }
            /* Initialize the new disk block 0. */
            bc_write (inode_disk->double_indirect_block_sec, (void*)ind_block,
                      0, BLOCK_SECTOR_SIZE, 0);
            free(ind_block);
          }
 
          ind_block = malloc (BLOCK_SECTOR_SIZE);
          if(bc_read(inode_disk->double_indirect_block_sec,
                   (void*)ind_block, 0, BLOCK_SECTOR_SIZE, 0))
          if(free_map_allocate(1, &ind_block->map_table[sec_loc.index1]))
          {
            bc_write (ind_block->map_table[sec_loc.index1], (void*)ind_block,
                    0, BLOCK_SECTOR_SIZE, 0);
/* change!! */            free(ind_block);
          }
        }
        /* update the newly allocated disk block number at INODE_DISK. */
        register_sector(inode_disk, sector_idx, sec_loc);
      }
      else
      {
        free(zeroes);
        return false;
      }
      /* Initialize the new disk block 0. */
      bc_write(sector_idx, zeroes, 0, BLOCK_SECTOR_SIZE, 0);
    }
    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
  }

  free(zeroes);
  return true;

}

/* Free all disk block allocated in INODE_DISK. */
static void
free_inode_sectors (struct inode_disk *inode_disk)
{
  struct inode_indirect_block *ind_block_1;
  int i, j;

  /* Free the double indirect disk block. */
  if(inode_disk->double_indirect_block_sec > 0)
  {
    ind_block_1 = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
    if (ind_block_1)
    {
      /* Read the first index block from buffer cache. */
      bc_read(inode_disk->double_indirect_block_sec,
              (void*)ind_block_1, 0, BLOCK_SECTOR_SIZE, 0);
    }

    struct inode_indirect_block *ind_block_2;
    i = 0;
    /* Access the second order index block through the first order index block. */
    while (ind_block_1->map_table[i] > 0)
    {
      ind_block_2 = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
      /* read the second order index block from buffer cache. */
      bc_read(ind_block_1->map_table[i],
              (void*)ind_block_2, 0, BLOCK_SECTOR_SIZE, 0);
      j = 0;
      /* Access the disk block number saved in the second index disk block. */
      while (ind_block_2->map_table[j] > 0)
      {
        /* Free the disk block by free_map update. */
        free_map_release (ind_block_2->map_table[j], 1);
        j++;
      }
      /* Free the second order index block. */
      free_map_release (ind_block_1->map_table[i], 1);
      i++;
      free(ind_block_2); // change!!!!!!!!
    }
    /* Free the first order index block. */
    free_map_release (inode_disk->double_indirect_block_sec, 1);
    free(ind_block_1); // change!!!!!!!!!!!
  }
  /* Free the indirect disk block. */
  if(inode_disk->indirect_block_sec > 0)
  {
    ind_block_1 = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
    if (ind_block_1)
    {
      /* read the index block from buffer cache. */
      bc_read(inode_disk->indirect_block_sec,
              (void*)ind_block_1, 0, BLOCK_SECTOR_SIZE, 0);
    }
    i = 0;
    /* Access the disk block number saved in the index block.  */
    while(ind_block_1->map_table[i] > 0)
    {
      /* Free the disk block by free_map update. */
      free_map_release (ind_block_1->map_table[i], 1);
      i++;
    }
    free_map_release (inode_disk->indirect_block_sec, 1);
    free(ind_block_1); // change!!!!!!!!!!!!
  }
  /* Free the direct disk block. */
  i = 0;
  while (inode_disk->direct_map_table[i] > 0)
  {
    /* Free the disk block by free_map update. */
    free_map_release (inode_disk->direct_map_table[i], 1);
    i++;
  }
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  struct inode_disk *disk_inode = NULL;
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          disk_inode = calloc (1, sizeof *disk_inode);
          if(disk_inode == NULL)
            return;
          /* Get the on-disk inode and return the disk block.
             return the on-disk inode and free DISK_INODE. */
          get_disk_inode(inode, disk_inode);
          free_inode_sectors(disk_inode);
          free_map_release (inode->sector, 1);
          free(disk_inode);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  struct inode_disk *disk_inode = NULL;


  disk_inode = calloc (1, sizeof *disk_inode);
  if(disk_inode == NULL)
    return 0;

  get_disk_inode(inode, disk_inode);
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (disk_inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = disk_inode->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      bc_read(sector_idx, (void *)buffer, bytes_read, 
              chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  free(disk_inode);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  struct inode_disk *disk_inode = NULL;

  if (inode->deny_write_cnt)
    return 0;

  /* Allocate the memory for DISK_INODE. */
  disk_inode = calloc (1, sizeof *disk_inode);
  if(disk_inode == NULL)
    return 0;
  /* Read the on-disk inode from buffer cache. */
  get_disk_inode(inode, disk_inode);

  lock_acquire(&inode->extend_lock);
  int old_length = disk_inode->length;
  int write_end = offset + size - 1;
  if (write_end > old_length -1)
  {
    /* Update the on-disk inode if the file size grows. */
    disk_inode->length = write_end + 1; 
    inode_update_file_length(disk_inode,
                         old_length, write_end);
  }
  lock_release(&inode->extend_lock);

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (disk_inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = disk_inode->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Read from BUFFER to buffer cache. */
      bc_write(sector_idx, (void *)buffer, bytes_written, 
               chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  /* Write DISK_INODE to buffer cache. (Use BC_WRITE) */
  bc_write (inode->sector, (void*)disk_inode,
                0, BLOCK_SECTOR_SIZE, 0);

  free(disk_inode);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk *disk_inode = NULL;
  off_t length;
  disk_inode = calloc (1, sizeof *disk_inode);
  if(disk_inode == NULL)
    return 0;
  get_disk_inode(inode, disk_inode);
  
  //change!!!!!!!!!!!!!!!!!!!!!!!!1  
  length = disk_inode->length;
  free(disk_inode);

  return length;
}



/* Return the whether is dir or not */
bool
inode_is_dir (const struct inode *inode)
{
  bool result;
  struct inode_disk *disk_inode = NULL;
  disk_inode = calloc (1, sizeof *disk_inode);
  if(disk_inode == NULL)
    return false;
  
  if(inode == NULL)
    return false;
  get_disk_inode(inode, disk_inode);
  result = (disk_inode->is_dir == 1);
  free(disk_inode);  /// change!!!!!!!!!!1

  return result;
}
/* Return the whether is removed or not */
bool
inode_is_rm (const struct inode *inode)
{
  return inode->removed;
}
int
inode_get_open_cnt(struct inode *inode)
{
  return inode->open_cnt;
}
