#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
  /* Set working dir */
  thread_current()->cur_dir = dir_open_root();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  if (name == NULL)
    return false;
  /* Get dir and file name */
  char *cp_name =  palloc_get_page (0);
  char *file_name =  palloc_get_page (0);
  if (cp_name == NULL)
    return false;
  strlcpy (cp_name, name, PGSIZE);

  struct dir *dir = parse_path(cp_name, file_name);
  bool success = false;
  /* Check cwd is removed */
  if(!inode_is_rm(dir_get_inode(thread_current()->cur_dir)))
  {
    /* Insert new file to direcory */ 
    success = (dir != NULL
               && free_map_allocate (1, &inode_sector)
               && inode_create (inode_sector, initial_size, 0)
               && dir_add (dir, file_name, inode_sector));

    if (!success && inode_sector != 0) 
      free_map_release (inode_sector, 1);
  
  }

  /* Free */
  dir_close (dir);
  palloc_free_page(cp_name);
  palloc_free_page(file_name);
  
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  if (name == NULL)
    return NULL;
  /* Get dir and file name */
  char *file_name =  palloc_get_page (0);
  char *cp_name =  palloc_get_page (0);
  if (cp_name == NULL || file_name == NULL)
    return NULL;
  strlcpy (cp_name, name, PGSIZE);

  struct dir *dir = parse_path(cp_name, file_name);

  struct inode *inode = NULL;
  /* Get inode of file */
  if (strlen(file_name) > 0)
  {
    /* Check cwd is removed */
    if(!inode_is_rm(dir_get_inode(thread_current()->cur_dir))
       && dir != NULL && dir_lookup (dir, file_name, &inode))
    {
      /* Get inode done. */
    }
  }
  else
  {
    if(dir != NULL)
      inode = dir_get_inode(dir);
  }

  /* Free */
  dir_close (dir);
  palloc_free_page(cp_name);
  palloc_free_page(file_name);
  
  if(inode != NULL)
    return file_open (inode);
  else
    return NULL;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  if (name == NULL)
    return false;
  /* Get dir and file name */
  char *file_name =  palloc_get_page (0);
  char *cp_name =  palloc_get_page (0);
  if (cp_name == NULL)
  {
    return false;
  }
  strlcpy (cp_name, name, PGSIZE);
  struct dir *dir = parse_path(cp_name, file_name);

  bool success = false;
  struct inode *inode;
 
  /* Get inode */
  if(file_name != NULL && dir_lookup(dir, file_name, &inode))
  {
    /* Case of dir */
    if(inode_is_dir(inode))
    {
      /* Check is empty dir */
      struct dir *rm_dir = dir_open(inode);
      
      if(!dir_readdir(rm_dir, file_name))
      {
        success = dir_remove (dir, file_name);
      }
    }
    /* Case of file */
    else 
    {
      success = dir_remove (dir, file_name);
    }
  }  

  dir_close (dir); 
  palloc_free_page(cp_name);
  palloc_free_page(file_name);
  return success;
}

/* Creates a dir named NAME.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create_dir (const char *name)
{
  block_sector_t inode_sector = 0;
  struct inode *inode;
  if (name == NULL)
    return false;

  /* Get dir and file name */
  char *cp_name =  palloc_get_page (0);
  char *file_name =  palloc_get_page (0);
  if (cp_name == NULL)
    return false;
  strlcpy (cp_name, name, PGSIZE);
  struct dir *dir = parse_path(cp_name, file_name);
  
 
  /* Add new dir to parent */
  bool success = (dir != NULL
             &&!dir_lookup(dir, file_name, &inode)
             && free_map_allocate (1, &inode_sector)
             && dir_create (inode_sector, 16)
             && dir_add (dir, file_name, inode_sector));

  if (!success && inode_sector != 0)
  {
    free_map_release (inode_sector, 1);
  }

  /* Add current and parent dir info */
  if(success && dir_lookup(dir, file_name, &inode))
  {
    struct dir *new_dir = dir_open(inode);
    dir_add(new_dir, ".", inode_get_inumber(inode));
    dir_add(new_dir, "..", inode_get_inumber(dir_get_inode(dir)));
    dir_close(new_dir);  
  }
  /* Free */
  dir_close (dir);
  palloc_free_page(cp_name);
  palloc_free_page(file_name);

  return success;
}

struct dir*
parse_path (char *path_name, char *file_name)
{

  struct dir *dir;
  struct inode *inode;

  if (path_name == NULL || strlen(path_name) == 0 ||
      file_name == NULL )
    return NULL;

  if(strcmp(path_name, "/")==0)
  {
    return dir_open_root();
  }

  /* Get path info */
  char *token, *nextToken, *savePtr;
  token = strtok_r (path_name, "/", &savePtr);
  nextToken = strtok_r (NULL, "/", &savePtr);
  /* Case of relative */
  if (*path_name != '/')
  { 
    if (thread_current()->cur_dir != NULL)
    {
      dir = dir_reopen(thread_current()->cur_dir);
    }
    else
    {
      dir = dir_open_root();
    }
  }
  /* Case of absolute */
  else
  {
    dir = dir_open_root();
  }

  while (token != NULL && nextToken != NULL){
    /* Look up dir and save inode */
    if(dir_lookup(dir, token, &inode)&&inode_is_dir(inode))
    {
      dir = dir_open(inode);
      token = nextToken;
      nextToken = strtok_r (NULL, "/", &savePtr);
    }
    else
    {
      dir_close(dir);
      return NULL;
    }
  }
  /* Save file_name */
  if(token != NULL)
  {
    strlcpy(file_name, token, strlen(token) + 1);
  }

  /* return dir point */
  return dir;
}





/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  struct dir *root = dir_open_root();
  dir_add(root, ".", ROOT_DIR_SECTOR);
  dir_close(root);
  free_map_close ();
  printf ("done.\n");
}
