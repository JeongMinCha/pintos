#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "vm/page.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

/* Project 1: Command Line Parsing. */
void argument_stack (char ** parse, int count, void ** esp);

/* Project 3: The Hierarchy of the processes. */
struct thread *get_child_process (int pid);
void remove_child_process (struct thread * cp);

/* Project 4: The file descriptor table. */
int process_add_file (struct file *f);
struct file *process_get_file (int fd);
void process_close_file (int fd);

/* Project 8: Virtual Memory. */
bool handle_mm_fault (struct vm_entry *vme);

#endif /* userprog/process.h */
