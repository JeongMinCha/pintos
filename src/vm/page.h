#ifndef VM_PAGE_H_
#define VM_PAGE_H_

#include "lib/kernel/hash.h"
#include "lib/kernel/bitmap.h"
#include "userprog/process.h"
#include "vm/swap.h"
#include "vm/frame.h"
#include <list.h>

/* The type of virtual address determines how we get 
   some data from some file. */
enum VM_TYPE
  {
    VM_BIN,   /* Load the data from a binary file. */
    VM_FILE,  /* Load the data from a mapping file. */
    VM_ALON   /* Load the data from a swap space. */
  };

/* Reserved Map identifiers. */
enum MAPID
  {
    CLOSE_ALL,   /* Close all mmap file */
  };

/* Memory Mapped File */
struct mmap_file
  {
    int mapid;
    struct file* file;
    struct list_elem elem;
    struct list vme_list;
  }; 

/* Represent physical page */
struct page
{
  void *kaddr;
  struct vm_entry *vme;
  struct thread *thread;
  struct list_elem lru;
};

/* This is the data structure that represents each pages in virtual 
   address space. Text, data, stack segments in address space of 
   some processes are represented by this vm_entry structure. */
struct vm_entry
  {
    uint8_t type;
    void *vaddr;
    bool writable;
    bool is_loaded;
    bool pinned;
    struct file* file;
    struct list_elem mmap_elem;
    size_t offset;
    size_t read_bytes;
    size_t zero_bytes;
    size_t swap_slot;
    struct hash_elem elem;
  };

struct vm_entry *find_vme (void *vaddr);
bool insert_vme (struct hash *vm, struct vm_entry *vme);
bool delete_vme (struct hash *vm, struct vm_entry *vme);
void vm_init (struct hash *vm);
void vm_destroy (struct hash *vm);
bool load_file (void *kaddr, struct vm_entry *vme);
void check_valid_buffer (void *buffer, unsigned size, void *esp, bool to_write);
void check_valid_string (const void *str, void *esp);
void unpin_ptr (void *vaddr);
void unpin_string (void *str);
void unpin_buffer (void *buffer, unsigned size);

#endif /* vm/page.h */
