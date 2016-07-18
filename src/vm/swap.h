#include "lib/kernel/bitmap.h"
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "vm/page.h"


void swap_init(void);
void swap_in(size_t used_index, void *kaddr);
size_t swap_out(void *kaddr);
