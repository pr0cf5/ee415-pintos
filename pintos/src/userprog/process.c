#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "vm/vpage.h"
#include "vm/swap.h"

struct lock filesys_lock;

static struct pid_allocator pid_allocator;
/* used to prevent race between exec/wait/exit */
static struct lock process_lock;

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, struct process_info *pi, void (**eip) (void), void **esp);

/*
  the caller of this function must enusre that argc has at least argc + 1 available
*/
static void
tokenize (char *input, char **argv, int *argc) {

  char *next_ptr, *cursor, *end_ptr;
  int i = 0;
  int cnt = 0;

  /* find number of spaces */
  cursor = input; 
  end_ptr = &input[strlen(input)];

  while (cursor != end_ptr) {
    if (*cursor == ' ') {
      cnt++;
    }
    cursor++;
  }

  cursor = strtok_r (input, " ", &next_ptr);

  while(cursor) {
    argv[i++] = cursor;
    cursor = strtok_r (NULL, " ", &next_ptr);
  }

  argv[i] = NULL;
  *argc = i;
}

struct signal_handler_info *signal_handler_info_allocate(int signum, void *handler) {
  struct signal_handler_info *shi = malloc(sizeof(struct pending_signal));
  if (shi == NULL) {
    return NULL;
  }
  shi->signum = signum;
  shi->handler = handler;
  return shi;
}

void signal_handler_info_release(struct signal_handler_info * shi) {
  free(shi);
}

struct signal_handler_info *get_signal_handler_info(struct process_info *pi, int signum) {
  for (struct list_elem *e = list_begin(&pi->signal_handler_infos);
    e != list_end(&pi->signal_handler_infos); e = list_next(e)) {
      struct signal_handler_info *shi = list_entry(e, struct signal_handler_info, elem);
      if (shi->signum == signum) {
        return shi;
      }
  }
  return NULL;
}

struct pending_signal *pending_signal_allocate(int signum) {
  struct pending_signal *ps = malloc(sizeof(struct pending_signal));
  if (ps == NULL) {
    return NULL;
  }
  ps->signum = signum;
  return ps;
}

void pending_signal_release(struct pending_signal *ps) {
  free(ps);
}

void pending_signal_handle(struct process_info *pi, struct pending_signal *ps) {
  struct signal_handler_info *shi = get_signal_handler_info(pi, ps->signum);
  if (shi != NULL) {
    printf("Signum: %d, Action: %p\n", shi->signum, shi->handler);
  }
}

pid_t pid_allocate() {
  pid_t pid;
  lock_acquire(&pid_allocator.pid_lock);
  pid = pid_allocator.last_pid++;
  lock_release(&pid_allocator.pid_lock);
  return pid;
}

void pid_release(pid_t pid) {
  return;
}

struct process_info *get_process_info(pid_t pid) {
  struct process_info *pi = thread_current()->process_info;
  for (struct list_elem *e = list_begin(&pi->children_pi);
    e != list_end(&pi->children_pi); e = list_next(e)) {
      struct process_info *child_pi = list_entry(e, struct process_info, elem);
      if (child_pi->pid == pid) {
        return child_pi;
      }
    }
  return NULL;
}

/*
  kernel threads that do not have a user process can have a process_info_allocate structure. 
  The only possible way for this to happen is the main thread running the task.
  Therefore, it is a waste of resource to initialize stdin and stdout here, but for coherency we just maintain it this way.
*/
struct process_info *process_info_allocate(struct semaphore *sema, struct process_info *parent_pi) {
  struct process_info *new;
  if ((new = malloc(sizeof(struct process_info))) == NULL) {
    return NULL;
  }
  new->pid = pid_allocate();
  new->thread = thread_current();
  new->exit_code = 0;
  new->sema = sema;
  new->status = PROCESS_RUNNING;
  new->parent_pi = parent_pi;
  new->is_critical = false;
  new->exe_file = NULL;
  strlcpy(new->file_name, "process-default", sizeof(new->file_name));
  list_init(&new->children_pi);
  list_init(&new->user_file_list);
  list_init(&new->mmap_entries_list);
  list_init(&new->signal_handler_infos);
  list_init(&new->pending_signals);
  lock_init(&new->pending_signals_lock);
  init_stdin(new);
  init_stdout(new);
  return new;
}

void process_info_release(struct process_info *pi) {
  if (pi->sema) {
    free(pi->sema);
    pi->sema = NULL;
  }
  pid_release(pi->pid);
  for (struct list_elem *e = list_begin(&pi->children_pi);
    e != list_end(&pi->children_pi); e = list_next(e)) {
      struct process_info *child_pi = list_entry(e, struct process_info, elem);
      /*
        Assume the following: A --> B --> C
        Then when A waits for B, process_info_release(B) will be called.
        However, if B exited from process_exit, C->parent will be set to NULL. 
      */
      //ASSERT(child_pi->parent_pi == pi);
      child_pi->parent_pi = NULL;
  }
  free(pi);
}

void process_info_set_exit_code(struct process_info *info, int exit_code) {
  info->exit_code = exit_code;
}

void process_init() {
  lock_init(&pid_allocator.pid_lock);
  pid_allocator.last_pid = 1;
  lock_init(&process_lock);
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
pid_t
process_execute (const char *cmd_line) 
{
  struct process_start_args *args;
  tid_t tid;
  struct semaphore *sema;
  struct process_info *pi;
  bool new_pi = false;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  args = palloc_get_page (0);
  if (args == NULL)
    return PID_ERROR;
  strlcpy (args->cmd_line, cmd_line, sizeof(args->cmd_line));

  /* initialize process info structure */
  
  if ((sema = malloc(sizeof(struct semaphore))) == NULL) {
    goto fail1;
  }
  if ((pi = thread_current()->process_info) == NULL) {
    new_pi = true;
    if ((pi = process_info_allocate(NULL, NULL)) == NULL) {
      goto fail1;
    }
  }
  sema_init(sema, 0);
  struct process_info *child_pi;
  args->sema = sema;
  args->parent_pi = pi;
  args->out_pi = &child_pi;

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (cmd_line, PRI_DEFAULT, start_process, args);
  if (tid == TID_ERROR) {
    // palloc_free_page need only be done here, because palloc_free_page is called within the child routine
    palloc_free_page(args); 
    goto fail2;
  }
  
  sema_down(sema);
  if (child_pi != NULL) {
    /* even if process_info existed before, this doesn't matter */
    thread_current()->process_info = pi;
    /* no need to call palloc_free_page(args), because it is called by the child */
    return child_pi->pid;
  }
  else {
    // child does not free sema
    free(sema);
  }

fail2:
  if (new_pi) {
    process_info_release(pi);
  }
fail1:
  return PID_ERROR;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *args_)
{
  struct process_start_args *args = args_;
  struct intr_frame if_;
  bool success = false;
  struct process_info *pi;
  struct process_info **out_pi;

  /* Initialize process_info structure */
  if ((pi = process_info_allocate(args->sema, args->parent_pi)) == NULL) {
    success = false;
    goto done;
  }

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load ((const char *)args->cmd_line, pi, &if_.eip, &if_.esp);

done:
  /* If load failed, quit. */
  out_pi = args->out_pi;
  palloc_free_page (args);
  if (!success) {
    /* 
      a very special case, where sema must not be freed until sema_up, 
      even though pi must be freed.
      sema is set to NULL and not freed in process_info_release, because if it is freed, 
      sema_down will operate on a freed semaphore, causing UaF. The seamphore must be freed by the parent.
    */
    *out_pi = NULL;
    sema_up(pi->sema);
    pi->sema = NULL;
    process_info_release(pi);
    thread_exit();    
  }
  thread_current()->process_info = pi;
  list_push_back(&pi->parent_pi->children_pi, &pi->elem);
  *out_pi = pi;
  sema_up(pi->sema);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (pid_t child_pid) 
{
  int return_value;
  struct process_info *pi = thread_current()->process_info;
  if (pi == NULL) {
    return_value = -1;
    goto done;
  }
  for (struct list_elem *e = list_begin(&pi->children_pi);
    e != list_end(&pi->children_pi); e = list_next(e)) {
      struct process_info *child_pi = list_entry(e, struct process_info, elem);
      if (child_pi->pid == child_pid) {
        sema_down(child_pi->sema);
        ASSERT(child_pi->status == PROCESS_EXITED);
        return_value = child_pi->exit_code;
        list_remove(&child_pi->elem);
        process_info_release(child_pi);
        goto done;
      }
  }
return_value = -1;
done:
  return return_value;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  if (cur->process_info) {
    struct process_info *pi = cur->process_info;
    /* free all mmap entries */
    {
      struct list_elem *cur, *next;
      cur = list_begin(&pi->mmap_entries_list);
      while(cur != list_end(&pi->mmap_entries_list)) {
        next = list_next(cur);
        struct mmap_entry *me = list_entry(cur, struct mmap_entry, elem);
        list_remove(&me->elem);
        mmap_entry_release(me);
        cur = next;
      }
    }

    /* free all vapge entries */
    {
      vpage_info_release_all(pi->pid);
    }

    /* free all user_file objects */
    {
      struct list_elem *cur, *next;
      cur = list_begin(&pi->user_file_list);
      while(cur != list_end(&pi->user_file_list)) {
        next = list_next(cur);
        struct user_file *uf = list_entry(cur, struct user_file, elem);
        list_remove(&uf->elem);
        user_file_release(uf);
        cur = next;
      }
    }

    /* free all pending_signals */
    {
      lock_acquire(&pi->pending_signals_lock);
      struct list_elem *cur, *next;
      cur = list_begin(&pi->pending_signals);
      while(cur != list_end(&pi->pending_signals)) {
        next = list_next(cur);
        struct pending_signal *ps = list_entry(cur, struct pending_signal, elem);
        list_remove(&ps->elem);
        pending_signal_release(ps);
        cur = next;
      }
      lock_release(&pi->pending_signals_lock);
    }

    /* free all signal_handler_infos */
    {
      struct list_elem *cur, *next;
      cur = list_begin(&pi->signal_handler_infos);
      while(cur != list_end(&pi->signal_handler_infos)) {
        next = list_next(cur);
        struct signal_handler_info *shi = list_entry(cur, struct signal_handler_info, elem);
        list_remove(&shi->elem);
        signal_handler_info_release(shi);
        cur = next;
      }
    }

    /* allow writes to executables by closing exe_file */
    if (pi->exe_file) {
      file_close(pi->exe_file);
    }    
    
    /* if pi has a parent, set exit code, and sema up. If it does not, free the pi structure */
    if (pi->parent_pi) {
      pi->status = PROCESS_EXITED;
      thread_current()->process_info = NULL;
      sema_up(pi->sema);
    }
    else {
      /*
        actually, this part is only reachable once, 
        because process_exit would have to be called on the main thread 
      */
      process_info_release(pi);
      thread_current()->process_info = NULL;
    }
  }
  pd = cur->pagedir;
  if (pd != NULL) 
  {
    /* Correct ordering here is crucial.  We must set
        cur->pagedir to NULL before switching page directories,
        so that a timer interrupt can't switch back to the
        process page directory.  We must activate the base page
        directory before destroying the process's page
        directory, or our active page directory will be one
        that's been freed (and cleared). */
    cur->pagedir = NULL;
    pagedir_activate (NULL);
    pagedir_destroy (pd);
  }
}
/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* handle pending signals */
  bool old_sema_yield = sema_yield;
  sema_yield = false;
  struct list_elem *cur, *next;
  struct process_info *pi = thread_current()->process_info;
  if (pi != NULL) {
    // this function is called within an interrupt handler, so no synchronization primitives
    cur = list_begin(&pi->pending_signals);
    while(cur != list_end(&pi->pending_signals)) {
      next = list_next(cur);
      struct pending_signal *ps = list_entry(cur, struct pending_signal, elem);
      pending_signal_handle(pi, ps);
      list_remove(&ps->elem);
      pending_signal_release(ps);
      cur = next;
    }
  }
  sema_yield = old_sema_yield;

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (struct process_info *pi, void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct process_info *pi, struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *cmd_line, struct process_info *pi, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  size_t file_name_len, cmd_line_len;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  file_name_len = strlen(cmd_line);
  cmd_line_len = strlen(cmd_line);

  for (int i = 0; i < cmd_line_len; i++) {
    if (cmd_line[i] == ' ') {
      file_name_len = i;
      break;
    }
  }

  if (file_name_len+1 >= sizeof(pi->file_name)) {
    ASSERT(0);
    goto done;
  }

  strlcpy(pi->file_name, cmd_line, file_name_len+1);

  /* Open executable file. */
  lock_acquire(&filesys_lock);
  file = filesys_open (pi->file_name);
  lock_release(&filesys_lock);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", pi->file_name);
      goto done; 
    }
  lock_acquire(&filesys_lock);
  file_deny_write(file);
  pi->exe_file = file;
  lock_release(&filesys_lock);
  if (pi->exe_file == NULL) {
    goto done;
  }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", pi->file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (pi, file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (pi, esp))
    goto done;

  /* setup stack */
  char *user_stack, *cmd_line_copy, *cursor;
  size_t argc;
  cmd_line_len = cmd_line_len % 4 == 0 ? cmd_line_len + 4 : cmd_line_len + 4 - (cmd_line_len%4);
  user_stack = (char *)(*esp);
  user_stack -= cmd_line_len;
  cmd_line_copy = user_stack;
  strlcpy(user_stack, cmd_line, cmd_line_len);

  argc = 1;
  cursor = cmd_line_copy;
  while (*cursor) {
    if (*cursor == ' '){
      argc++;
    }
    cursor++;
  }

  if (argc > ARGC_LIMIT) {
    success = false;
    goto done;
  }

  user_stack -= (argc + 1) * sizeof(void *);
  tokenize(cmd_line_copy, (char **)user_stack, &argc);
  {
    void **_user_stack = (void **)user_stack;
    _user_stack[-1] = user_stack;
    _user_stack[-2] = (void *)argc;
    _user_stack[-3] = (void *)0xcafebebe;
    *esp = &_user_stack[-3];
  }

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;
  
 done:
  /* 
    We arrive here whether the load is successful or not. 
  */
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);
static bool install_hpage(void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct process_info *pi, struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

#ifdef VM
  while (read_bytes > 0 || zero_bytes > 0) 
  {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
      size_t hpage_read_bytes = read_bytes < HPGSIZE ? read_bytes : HPGSIZE;
      size_t hpage_zero_bytes = HPGSIZE - hpage_read_bytes;
    if (read_bytes + zero_bytes >= HPGSIZE && ((uint32_t) pg_round_down(upage) & HPGMASK) == 0) {
      if (!vpage_info_lazy_allocate(upage, file, ofs, hpage_read_bytes, pi->pid, writable, true)) 
      {
        return false; 
      }

      /* Advance. */
      read_bytes -= hpage_read_bytes;
      zero_bytes -= hpage_zero_bytes;
      upage += HPGSIZE;
    }
    else {
      if (!vpage_info_lazy_allocate(upage, file, ofs, page_read_bytes, pi->pid, writable, false)) 
      {
        return false; 
      }

      /* Advance. */
      ofs += page_read_bytes;
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  }
  return true;
#else
  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
#endif
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (struct process_info *pi, void **esp) 
{
  #define STACK_GROWTH_PAGES 0
  uint8_t *kpage, *upage;
  struct vpage_info *vpi_stack;
  struct vpage_info *vpi_growth[STACK_GROWTH_PAGES];
  pid_t pid;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  upage = (uint8_t *)PHYS_BASE - PGSIZE;
  pid = pi->pid;

  if (!kpage) {
    goto fail_nofree;
  }
  vpi_stack = vpage_info_inmem_allocate(upage, kpage, pid, true);
  // assme stack can grow up to two pages
  if (!vpi_stack) {
    goto fail;
  }
  for (int i = 0; i < STACK_GROWTH_PAGES; i++) {
    if (!(vpi_growth[i] = vpage_info_lazy_allocate(upage - PGSIZE*(i+1), NULL, 0, 0, pid, true, false))) {
      vpage_info_release(vpi_stack);
      for (int j = 0; j < i; j++) {
        vpage_info_release(vpi_growth[j]);
      }
      // vpage_info_release frees kpage, so no freeing
      goto fail_nofree;
    }
  }
  *esp = upage + PGSIZE;
  return true;
fail:
  palloc_free_page(kpage);
fail_nofree:
  return false;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable, false));
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table, where huge paging is enabled */
static bool
install_hpage (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable, true));
}
