#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/off_t.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"
#include "filesys/directory.h"
#include "userprog/pagedir.h"
#include "vm/page.h"

static void syscall_handler (struct intr_frame *);

struct vm_entry *check_address (void *, void *);
void get_argument (void *, int *, int);

/* System call functioni list */
/* 0:  SYS_HALT */     void halt (void);
/* 1:  SYS_EXIT */     void exit (int);
/* 2:  SYS_EXEC */     tid_t exec (const char*);
/* 3:  SYS_WAIT */     int wait (tid_t);
/* 4:  SYS_CREATE */   bool create (const char *, unsigned);
/* 5:  SYS_REMOVE */   bool remove (const char *);             
/* 6:  SYS_OPEN */     int open (const char *);
/* 7:  SYS_FILESIZE */ int filesize (int);
/* 8:  SYS_READ */     int read (int, void *, unsigned);
/* 9:  SYS_WRITE */    int write (int, void *, unsigned);
/* 10: SYS_SEEK */     void seek (int, unsigned);
/* 11: SYS_TELL */     unsigned tell (int);
/* 12: SYS_CLOSE */    void close (int);
/* 13: SYS_MMAP */     int mmap (int, void *);
/* 14: SYS_MUNMAP */   void munmap (int);
/* 15: SYS_CHDIR  */   bool sys_chdir(const char *);
/* 16: SYS_MKDIR  */   bool sys_mkdir(const char *);
/* 17: SYS_READDIR*/   bool sys_readdir(int, char *);
/* 18: SYS_ISDIR  */   bool sys_isdir(int);
/* 19: SYS_INUMBER */  int sys_inumber(int);



const int arg_count[] =
{
#define MAX_ARGS 3
  0, /* SYS_HALT */
  1, /* SYS_EXIT */
  1, /* SYS_EXEC */
  1, /* SYS_WAIT */
  2, /* SYS_CREATE */
  1, /* SYS_REMOVE */
  1, /* SYS_OPEN */
  1, /* SYS_FILESIZE */
  3, /* SYS_READ */
  3, /* SYS_WRITE */
  2, /* SYS_SEEK */
  1, /* SYS_TELL */
  1, /* SYS_CLOSE */
  2, /* SYS_MMAP */
  1, /* SYS_MUNMAP */
  1, /* SYS_CHDIR */
  1, /* SYS_MKDIR */
  2, /* SYS_READDIR */
  1, /* SYS_ISDIR */
  1, /* SYS_INUMBER */
};

void
syscall_init (void) 
{
  lock_init (&file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int arg[MAX_ARGS];
  uint32_t *stack_ptr = f->esp;
  int number = *stack_ptr;

  check_address(stack_ptr, f->esp);
  if(stack_ptr)
  {
    /* Get the arguments from stack pointer. */
    get_argument(stack_ptr, &arg[0], arg_count[number]+1);

    /* 'number' = System call number. */
    switch(number)
    {
    case SYS_HALT:/* 0 */
      halt();
    break;
    
    case SYS_EXIT:/* 1 */
      exit(*(int *)arg[0]);
    break;
  
    case SYS_EXEC:/* 2 */
      check_valid_string ((const void *)arg[0], f->esp);
      f->eax = exec((const char *)*(int *)arg[0]);
      unpin_string ((void *)arg[0]);
    break;
    
    case SYS_WAIT:/* 3 */
      f->eax = wait(*(int *)arg[0]);
    break;
    
    case SYS_CREATE:/* 4 */
      check_valid_string ((const void *)arg[0], f->esp);
      f->eax = create((const char *)*(int *)arg[0], (unsigned)*(int *)arg[1]);
      unpin_string ((void *)arg[0]);
    break;
    
    case SYS_REMOVE:/* 5 */
      check_valid_string ((const void *)arg[0], f->esp);
      f->eax = remove((char *)*(int *)arg[0]);
      unpin_string ((void *)arg[0]);
    break;
    
    case SYS_OPEN:/* 6 */
      check_valid_string ((const void *)arg[0], f->esp);
      f->eax = open((const char *)*(int *)arg[0]);
      unpin_string ((void *)arg[0]);
    break;
    
    case SYS_FILESIZE:/* 7 */
      f->eax = filesize(*((int *)arg[0]));
    break;
    
    case SYS_READ:/* 8 */
      check_valid_buffer ((void *)*(int *)arg[1], (unsigned)*(int *)arg[2], f->esp, true);
      f->eax = read(*(int *)arg[0], (void *)*(int *)arg[1], (unsigned)*(int *)arg[2]);
      unpin_buffer ((void *)arg[1], (unsigned)*(int *)arg[2]);
    break;
    
    case SYS_WRITE:/* 9 */
      check_valid_buffer ((void *)*(int*)arg[1], (unsigned)*(int *)arg[2], f->esp, false); 
      f->eax = write(*(int *)arg[0], (void *)*(int *)arg[1], (unsigned)*(int *)arg[2]);
      unpin_buffer ((void *)arg[1], (unsigned)*(int *)arg[2]);
    break;
    
    case SYS_SEEK:/* 10 */
      seek(*(int *)arg[0], (unsigned)*(int *)arg[1]);
    break;
    
    case SYS_TELL:/* 11 */
      f->eax = tell(*(int *)arg[0]);
    break;
    
    case SYS_CLOSE:/* 12 */
      close(*(int *)arg[0]);
    break;

    case SYS_MMAP:/* 13 */
      f->eax = mmap(*(int *)arg[0], (void *)*(int *)arg[1]);
	break;

    case SYS_MUNMAP:/* 14 */
      munmap(*(int *)arg[0]);
	break;
 
    case SYS_CHDIR:/* 15 */
      check_valid_string ((const void *)arg[0], f->esp);
      f->eax = sys_chdir((char *)*(int *)arg[0]);
      unpin_string ((void *)arg[0]);
	break;

    case SYS_MKDIR:/* 16 */
      check_valid_string ((const void *)arg[0], f->esp);
      f->eax = sys_mkdir((char *)*(int *)arg[0]);
      unpin_string ((void *)arg[0]);
	break;

    case SYS_READDIR:/* 17 */
      check_valid_string ((const void *)arg[1], f->esp);
      f->eax = sys_readdir(*(int *)arg[0], (char *)*(int *)arg[1]);
      unpin_string ((void *)arg[0]);
	break;

    case SYS_ISDIR:/* 18 */
      f->eax = sys_isdir(*(int *)arg[0]);
	break;

    case SYS_INUMBER:/* 19 */
      f->eax = sys_inumber(*(int *)arg[0]);
	break;
    
    default:
      {
        printf("System call number (%d) is not supported.\n", number);
        thread_exit();
      }
    break;
    } /* switch end. */
    unpin_ptr (f->esp); /* Unpin the stack pointer. */
  }
  else
    {
      thread_exit ();
    }
}


struct vm_entry *
check_address(void *addr, void *esp)
{
  /* Check if ADDR is in USER_SPACE. */
  if(addr < (void *)0x08048000 || addr >= (void *)0xc0000000)
  {
    exit (-1);
  }

  /* returns VM_ENTRY with the given address ADDR. */
  struct vm_entry *vme = find_vme (addr);

  if(vme)
    return vme;
  else
    return NULL;
}

/* arguments in ESP will be saved into ARG. The number of the
   arguments is 'count'. After This func conducted, the elements
   of ARG will be addresses for the arguments. */
void
get_argument (void *esp, int *arg, int count)
{ 
  int i=0;
  int *arg_address;

  for(i=0; i<count; i++)
  {
    arg_address = (int *)esp +i+1;
    arg[i] = arg_address;
    check_address ((void *)arg_address, esp);
  }
}

/* SYS_HALT: 0 */
void
halt (void)
{
  shutdown_power_off();
}

/* SYS_EXIT: 1 */
void
exit(int status)
{
  struct thread *cur = thread_current ();

  cur->exit_status = status;
  printf("%s: exit(%d)\n", cur->name, status);
  thread_exit();
}

/* SYS_EXEC: 2 */
tid_t
exec (const char *cmd_line)
{
  tid_t tid = process_execute(cmd_line);
  struct thread * child = get_child_process(tid);

  sema_down(&child->load_semaphore);
  if(child->is_loaded == 1)
    return tid;
  else
    return -1;
}

/* SYS_WAIT: 3 */
int
wait (tid_t tid)
{
  return process_wait(tid);
}

/* SYS_CREATE: 4 */ 
bool
create (const char * file, unsigned initial_size)
{
  if(file && initial_size >= 0)
    return filesys_create(file, initial_size);
  else
    exit (-1);
}

/* SYS_REMOVE: 5 */ 
bool
remove (const char *file)
{
  return filesys_remove(file);
}


/* 6:  SYS_OPEN */     
int 
open (const char *file)
{
  if(file == NULL)
    return -1;

  struct file *fp = filesys_open (file);
  int fd = -1;

  if(fp)
    fd = process_add_file (fp);
  else
    return -1;

  return fd;
}

/* 7:  SYS_FILESIZE */ 
int 
filesize (int fd)
{
  struct file *fp = process_get_file(fd);

  if(fp)
    return (int) file_length (fp);
  else
    return -1;
}

/* 8:  SYS_READ */     
int 
read (int fd, void *buffer, unsigned size)
{
  lock_acquire(&file_lock);
  /* Standard Input (STDIN) */
  if(fd == 0)
  {
    int i=0;
    uint8_t *local_buffer = (uint8_t *) buffer;
    for(i=0; i<size; i++)
    {
      local_buffer[i] = input_getc ();
    }
    lock_release (&file_lock);
    return size;
  }

  struct file *fp = process_get_file(fd);
  int bytes = 0;
  
  if(!fp)
  {
    lock_release (&file_lock);
    return -1;
  }
  
  bytes = file_read (fp, buffer, size);
  lock_release (&file_lock);
  
  return bytes;
}

int 
write (int fd, void *buffer, unsigned size)
{
  lock_acquire (&file_lock);
  /* Standard Output (STDOUT) */
  if(fd == 1)
  {
    putbuf(buffer, size);
    lock_release (&file_lock);
    return size;
  }

  struct file *fp = process_get_file(fd);
  int bytes = 0;

  if(!fp || inode_is_dir (file_get_inode(fp)))
  {
    lock_release (&file_lock);
    return -1;
  }

  bytes = (int)file_write (fp, buffer, size);
  lock_release (&file_lock);

  return bytes;
}

/* 10: SYS_SEEK */     
void 
seek (int fd, unsigned position)
{
  struct file *fp = process_get_file(fd);

  if(fp)
    file_seek (fp, position);
}

/* 11: SYS_TELL */     
unsigned
tell (int fd)
{
  struct file *fp = process_get_file(fd);
  off_t offset;
  
  if(!fp)
  {
    lock_release (&file_lock);
    return -1;
  }

  offset = file_tell (fp);
  return offset;
}

/* 12: SYS_CLOSE */    
void 
close (int fd)
{
  /* STDIN or STDOUT, exit -1. */
  if(fd == 0 || fd == 1)
     exit (-1);

  process_close_file(fd);
}

/* 13: SYS_MMAP */
int
mmap(int fd, void *addr)
{
  struct file *fp = process_get_file (fd);
  struct mmap_file *mmp;
  int mapid = thread_current() -> mapid++;

  /* Check ADDR and file pointer */
  if (!fp || !is_user_vaddr(addr) || ((uint32_t) addr % PGSIZE) != 0 || 
     (addr < (void *)0x08048000 || addr >= (void *)0xc0000000))
  {
    return -1;
  }
  else
  {
    fp = file_reopen (fp);
    /* Allocate the memory for MMAP FILE and initialize it. */
    mmp = (struct mmap_file*) malloc (sizeof(struct mmap_file));
    mmp->file = fp;
    mmp->mapid = mapid;
    list_init(&mmp->vme_list);
  } 

  int32_t ofs = 0;
  uint32_t read_bytes = file_length(fp);
  while (read_bytes > 0)
    {
       /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      uint32_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      uint32_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Allocate the memory for VM_ENTRY. */
      struct vm_entry *vme = (struct vm_entry *)
                             malloc(sizeof(struct vm_entry));
      /* Initialize the fields of the VM_ENTRY. */
      vme->type = VM_FILE;
      vme->vaddr = addr;
      vme->writable = true;
      vme->is_loaded = false;
      vme->pinned = false;
      vme->file = fp;
      vme->offset = ofs;
      vme->read_bytes = page_read_bytes;
      vme->zero_bytes = page_zero_bytes;

      /* Insert the VM_ENTRY to hash table VM. */
      if(!insert_vme (&thread_current ()->vm, vme))
      {
        /* Insert fail, unmap the memory. */
        do_munmap(mmp);
        free(mmp);
        return -1;
      }
      else
      {
        /* Insert the VM_ENTRY to the vme list */
        list_push_back(&mmp->vme_list, &vme->mmap_elem);
      }
      /* Advance */
      read_bytes -= page_read_bytes;
      ofs += page_read_bytes;
      addr += PGSIZE;
  }
  /* Insert MMAP FILE to MMAP LIST of the current thread. */
  list_push_back(&thread_current()->mmap_list, &mmp->elem);
  return mapid;
}

void
do_munmap(struct mmap_file *mmap_file)
{
  struct thread *t = thread_current();

  /* Remove all VM_ENTRY in VME_LIST of MMAP_FILE. */
  while (!list_empty(&mmap_file->vme_list))
  {
    struct vm_entry *vme = list_entry (list_pop_front(&mmap_file->vme_list),
                                        struct vm_entry, mmap_elem);
    /* If the physical memory for VM_ENTRY exists and the page is dirty, 
       write the page to DISK. */
    if(vme->is_loaded && pagedir_is_dirty (t->pagedir, vme->vaddr))
    {
      lock_acquire (&file_lock);
      file_write_at(vme->file, vme->vaddr, vme->read_bytes, vme->offset);
      lock_release (&file_lock);
      free_page(pagedir_get_page(t->pagedir, vme->vaddr));
      pagedir_clear_page(t->pagedir, vme->vaddr);
    }

    /* Delete the VM_ENTRY. */
    if(delete_vme(&t->vm, vme))
    {
      free(vme);
    }
  }
}

/* 14: SYS_MUNMAP */
void
munmap (int mapping)
{
  struct thread *t = thread_current();
  struct list_elem *prev_e, *e = list_begin(&t->mmap_list);

  /* Remove mmap */ 
  while (e != list_end (&t->mmap_list))
  {
    struct mmap_file *mmp = list_entry (e, struct mmap_file, elem);
    /* Remove mmap that matched mapping or all */ 
    if (mmp->mapid == mapping || mapping == CLOSE_ALL)
    {
      prev_e = list_prev(e);
      do_munmap(mmp);
      list_remove(e);
      free(mmp);
      e = prev_e;
    }
    e = list_next(e);
  }
}

/* 15: SYS_CHDIR */
bool
sys_chdir(const char *path)
{ 
  bool success = false; 
  /* Get dir and file name */
  char *cp_name =  palloc_get_page (0);
  char *file_name =  palloc_get_page (0);
  if (cp_name == NULL|| file_name == NULL)
  {
    return success;
  }
  strlcpy (cp_name, path, PGSIZE);
  struct dir *dir = parse_path(cp_name, file_name);
  
  /* Change dir to file_name */
  struct inode *inode;
  if(dir != NULL && strlen(file_name) > 0)
  {
    if(dir_lookup(dir, file_name, &inode))
    {
      dir_close(thread_current()->cur_dir);
      thread_current()->cur_dir = dir_open(inode);
	  success = true;
    }
  }
  else
  {
    dir_close(thread_current()->cur_dir);
    thread_current()->cur_dir = dir_open_root();
	success = true;
  }
  
  /* Free */ 
  dir_close (dir);
  palloc_free_page(cp_name);
  palloc_free_page(file_name);
  
  return success;

}

/* 16: SYS_MKDIR */
bool sys_mkdir (const char *dir)
{
  return filesys_create_dir(dir);
}

/* 17: SYS_READDIR */
bool
sys_readdir(int fd, char *name)
{
  struct file *fp = process_get_file(fd);

  if(inode_is_dir (file_get_inode(fp)))
  { 
    if(!dir_readdir((struct dir*)fp, name))
      return false;
  }
  else
    return false;

  return true;
}
/* 18: SYS_ISDIR */
bool
sys_isdir(int fd)
{
  struct file *fp = process_get_file(fd);
  return inode_is_dir (file_get_inode(fp));
}

/* 19: SYS_INUMBER */
int
sys_inumber(int fd)
{
  struct file *fp = process_get_file(fd);
  if(fp == NULL)
    return -1;
  return inode_get_inumber (file_get_inode(fp));
}

