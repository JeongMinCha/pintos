#include "vm/frame.h"
#include "lib/kernel/hash.h"
#include "lib/kernel/bitmap.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "userprog/syscall.h"
#include <stdint.h>
#include <list.h>

struct list lru_list;
struct list_elem *lru_clock;
struct lock lru_list_lock;
static struct list_elem* get_next_lru_clock(void);

void
lru_list_init(void)
{
  /* Initialize LRU list and LRU list lock. */
  list_init(&lru_list);
  lock_init(&lru_list_lock);
  lru_clock = NULL;
}

struct page*
alloc_page ( enum palloc_flags flags )
{
  struct page *page;
  void *kaddr = palloc_get_page(flags);

  /* If cannot allocate a page any more, evict a page. */
  if(kaddr == NULL)
  {
    /* Select victim page and evict it! */
    kaddr = try_to_free_pages(flags);
  }
  page = (struct page*) malloc (sizeof(struct page));

  /* Initialize page */
  page->kaddr = kaddr;
  page->vme = NULL; 
  page->thread = thread_current();

  /* Insert page to lru list */
  lock_acquire(&lru_list_lock);
  add_page_to_lru_list(page);
  lock_release(&lru_list_lock);

  return page;
}

void 
free_page(void *kaddr)
{
  struct list_elem *e = list_begin (&lru_list);

  lock_acquire(&lru_list_lock);

  /* Search the entry in the LRU_LIST to free. */
  while(e!=list_end(&lru_list))
  {
    struct page *page = list_entry (e, struct page, lru);

    /* Search Finished, Free the page and Get Out. */
    if(page->kaddr == kaddr)
    {
      __free_page(page);
      break;
    }
    e = list_next (e);
  }
  lock_release(&lru_list_lock);
}

void 
__free_page(struct page *page)
{
  del_page_from_lru_list(page);
  palloc_free_page(page->kaddr);
  pagedir_clear_page (page->thread->pagedir, page->vme->vaddr);
  free(page);
}

void add_page_to_lru_list(struct page *page)
{
  list_push_back(&lru_list, &page->lru);
}

void del_page_from_lru_list(struct page *page)
{
  list_remove(&page->lru);
}

static struct list_elem* 
get_next_lru_clock(void)
{
  lru_clock = list_next(lru_clock);

  /* If LRU_CLOCK is the end elem, it returns NULL. */
  if(lru_clock == list_end(&lru_list))
    return NULL;
  else
    return lru_clock;
}
void *
try_to_free_pages(enum palloc_flags flags)
{

  void* kaddr;
  lock_acquire(&lru_list_lock);
  /* Wake up thread in sleep_list */
  while (true)
  {
    if(lru_clock == NULL)
      lru_clock = list_begin(&lru_list);

    struct page *page = list_entry(lru_clock, struct page, lru);
    lru_clock = get_next_lru_clock();

    /* If accessed bit is 1, set it 0. */
    if(!page->vme->pinned &&
       pagedir_is_accessed(page->thread->pagedir, page->vme->vaddr))
    {
      pagedir_set_accessed (page->thread->pagedir, page->vme->vaddr, false);
    }
    /* If accessed bit is 0, set the page victim. 
       The victim page will be evicted at this time. */ 
    else if(!page->vme->pinned)
    {
      /* check the vm_entry type. */
      switch(page->vme->type)
      {
        case VM_BIN:
          page->vme->type = VM_ALON;
        break;

        case VM_FILE:
          if(pagedir_is_dirty (page->thread->pagedir, page->vme->vaddr))
          {
            lock_acquire (&file_lock);
            /* Save the changes to the file. */
            file_write_at(page->vme->file, page->vme->vaddr, 
                          page->vme->read_bytes, page->vme->offset);
            lock_release (&file_lock);
          }
          page->vme->type = VM_ALON;
        break;
        case VM_ALON:
        break;
      }
      /* Swap out and free page */
      page->vme->swap_slot = swap_out (page->kaddr);
      page->vme->is_loaded = false;
      __free_page (page);

      /* allocate a page and check */
      kaddr = palloc_get_page(flags);
      if(kaddr)
        break;
    }
  }
  lock_release(&lru_list_lock);
  
  return kaddr;
}     
