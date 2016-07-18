#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "threads/thread.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

static unsigned vm_hash_func (const struct hash_elem *e, void *aux UNUSED);
static bool vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
static void vm_destroy_func (struct hash_elem *e, void *aux UNUSED);

/* Finds vm_entry with given address in the address of the current
   thread and returns it. If can't find it, returns NULL. */
struct vm_entry *find_vme (void *vaddr)
{
  struct hash_elem *e;

  /* vm_entry with given address. 
     pg_round_down returns the page number of VADDR. */
  struct vm_entry vme;
  vme.vaddr = pg_round_down(vaddr); 

  /* Find the hash element in the hash table VM
     in the current thread. */
  e = hash_find (&thread_current ()->vm, &vme.elem);

  if (e != NULL)
    return hash_entry (e, struct vm_entry, elem);
  else
    return NULL;
}

bool insert_vme (struct hash *vm, struct vm_entry *vme)
{
  /* In hash_insert function,
     If INSERT_SUCCESS, then it returns NULL.
     If INSERT_FAIL, then it doesn't return NULL. */
  if(hash_insert (vm, &vme->elem) == NULL)
    return true;
  else
    return false;
}

bool delete_vme (struct hash *vm, struct vm_entry *vme)
{
  /* In hash_delete function,
     If DELETE_SUCCESS, then it doesn't return NULL.
     If DELETE_FAIL, then it returns NULL. */
  if(hash_delete (vm, &vme->elem) != NULL)
    return true;
  else
    return false;
}

/* Returns a hash value for vm_entry E. */
static unsigned vm_hash_func (const struct hash_elem *e,
                              void *aux UNUSED)
{
  struct vm_entry *vme = hash_entry (e, struct vm_entry, elem);

  /* hash_int () returns a hash value for Fowler-Noll-Vo (FNV) 32-bit. */
  return hash_int ((int)vme->vaddr);
}

/* Returns TRUE if A's vaddr is more than B's vaddr. */
static bool vm_less_func (const struct hash_elem *a, 
                          const struct hash_elem *b,
                          void *aux UNUSED)
{
  struct vm_entry *vme_a = hash_entry (a, struct vm_entry, elem);
  struct vm_entry *vme_b = hash_entry (b, struct vm_entry, elem);

  if(vme_a->vaddr < vme_b->vaddr)
    return true;
  else
    return false;
}

/* This function will be used in VM_DESTORY */
static void vm_destroy_func (struct hash_elem *e,
                             void *aux UNUSED)
{
  struct vm_entry *vme = hash_entry (e, struct vm_entry, elem);
  /* If VME is still loaded, we should clear the page of VME. */
  if(vme->is_loaded)
  {
    free_page (pagedir_get_page (thread_current ()->pagedir, vme->vaddr));     
    pagedir_clear_page (thread_current ()->pagedir, vme->vaddr);
  }
  free (vme);
}

/* Initialize and make hash table VM. */
void vm_init (struct hash *vm)
{
  hash_init (vm, (hash_hash_func *)vm_hash_func, 
             (hash_less_func *)vm_less_func, NULL);
}

/* Destroy the hash table VM. */
void vm_destroy (struct hash *vm)
{
  hash_destroy (vm, (hash_action_func *)vm_destroy_func);
}

/* Read bytes from VME <file, offset>, 
   Load the file to KADDR. */
bool load_file (void *kaddr, struct vm_entry *vme)
{
  if(vme->read_bytes > 0)
  {
//    lock_acquire(&file_lock);
    /* read bytes from VME to KADDR as much as READ_BYTES. */
    if(vme->read_bytes != file_read_at(vme->file, 
    	         kaddr, vme->read_bytes, vme->offset))
    {
      /* Read bytes doesn't match, delete VME. */
      delete_vme (&thread_current()->vm, vme);
//      lock_release(&file_lock);
      return false;
    }
//    lock_release(&file_lock);
    /* Initialize 0 as much as ZERO_BYTES. */
    memset (kaddr+vme->read_bytes, 0, vme->zero_bytes);
  }

  return true;
}

void check_valid_buffer (void *buffer, unsigned size, void *esp, bool to_write)
{
  char *local_buffer = (char *)buffer;
  int i = 0;

  for (i=0; i<size; i++)
  {
    struct vm_entry *vme = check_address (local_buffer, esp);

    /* Check if we can write something in VM_ENTRY. */
    if(vme != NULL && to_write)
    {
      /* If VM_ENTRY is not writable, Error! */
      if(!vme->writable)
      {
        exit (-1);
      }
    }
    local_buffer ++;
  }
}

void check_valid_string (const void *str, void *esp)
{
  check_address (str, esp);
  /* Until encountering a null string,
     check the address of each character in STR. */
  while(*(char *)str != 0)
  {
    str = (char *)str+1;
    check_address (str, esp);
  }
}

/* Unpin the VM_ENTRY with the given address VADDR. */
void unpin_ptr (void *vaddr)
{
  struct vm_entry *vme = find_vme (vaddr);

  /* Find vm_entry and unpin it. */
  if(vme)
  {
    vme->pinned = false;
  }
}

/* Unpin STR. */
void unpin_string (void *str)
{
  unpin_ptr (str);
  /* Until encounter a null string,
     unpin each character in STR. */
  while (*(char *)str != '\0')
  {
    str = (char *)str + 1;
    unpin_ptr (str);
  }
}

/* Unpin BUFFER with the SIZE. */
void unpin_buffer (void *buffer, unsigned size)
{
  int i = 0; 
  char *local_buffer = (char *) buffer;

  for(i=0; i<size; i++)
  {
    unpin_ptr (local_buffer ++);
  }
}
