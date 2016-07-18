#include "lib/kernel/hash.h"
#include "lib/kernel/bitmap.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "vm/page.h"
#include <stdint.h>
#include <list.h>

void lru_list_init(void);

struct page* alloc_page(enum palloc_flags flags);
void free_page(void *kaddr);
void __free_page(struct page *page);
void add_page_to_lru_list(struct page *page);
void del_page_from_lru_list(struct page *page);
void* try_to_free_pages(enum palloc_flags flags);
