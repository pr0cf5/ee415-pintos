#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "userprog/usermem.h"

static struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);

static bool user_file_less(struct list_elem *e1, struct list_elem *e2) {
  struct user_file *f1 = list_entry(e1, struct user_file, elem);
  struct user_file *f2 = list_entry(e2, struct user_file, elem);
  return f1->fd < f2->fd;
}

bool init_stdin(struct process_info *pi) {
  struct user_file *f = malloc(sizeof(struct user_file));
  if (f == NULL) {
    return false;
  }
  f->fd = STDIN_FILENO;
  f->type = UserFileStdin;
  f->inner.file = NULL;
  list_insert_ordered(&pi->user_file_list, &f->elem, user_file_less, NULL);
  return true;
}

bool init_stdout(struct process_info *pi) {
  struct user_file *f = malloc(sizeof(struct user_file));
  if (f == NULL) {
    return false;
  }
  f->fd = STDOUT_FILENO;
  f->type = UserFileStdout;
  f->inner.file = NULL;
  list_insert_ordered(&pi->user_file_list, &f->elem, user_file_less, NULL);
  return true;
}

/* allocates fd and user_file structure for dir, and appends it to pi's list */
bool append_dir(struct process_info *pi, struct dir *dir) {
  struct user_file *f = malloc(sizeof(struct user_file));
  if (f == NULL) {
    return false;
  }
  f->fd = fd_allocate(pi);
  f->type = UserFileDir;
  f->inner.dir = dir;
  list_insert_ordered(&pi->user_file_list, &f->elem, user_file_less, NULL);
  return true;
}

/* allocates fd and user_file structure for file, and appends it to pi's list */
bool append_file(struct process_info *pi, struct file *file) {
  struct user_file *f = malloc(sizeof(struct user_file));
  if (f == NULL) {
    return false;
  }
  f->fd = fd_allocate(pi);
  f->type = UserFileFile;
  f->inner.file = file;
  list_insert_ordered(&pi->user_file_list, &f->elem, user_file_less, NULL);
  return true;
}

struct user_file *get_user_file(struct process_info *pi, int fd) {
  for (struct list_elem *e = list_begin(&pi->user_file_list); 
    e != list_end(&pi->user_file_list);
    e = list_next(e)) {
    struct user_file *f = list_entry(e, struct user_file, elem);
    if (f->fd == fd) {
      return f;
    }
  }
  return NULL;
}

bool remove_user_file(struct process_info *pi, int fd) {
  struct user_file *f = get_user_file(pi, fd);
  if (f == NULL) {
    return false;
  }
  list_remove(&f->elem);
  free(f);
}

int fd_allocate(struct process_info *pi) {
  int new_fd = 0;
  for (struct list_elem *e = list_begin(&pi->user_file_list); 
    e != list_end(&pi->user_file_list);
    e = list_next(e)) {
    struct user_file *f = list_entry(e, struct user_file, elem);
    if (f->fd == new_fd) {
      new_fd++;
    }
    else {
      break;
    }
  }
  return new_fd;
}

static char *strdup_user(const char *user_string) {
  size_t length = 0;
  for (size_t i = 0 ;; i++) {
    char c;
    if (copy_from_user(&c, user_string+i, 1) != 1) {
      return NULL;
    }
    if (c == '\0') {
      length = i;
      break;
    }
  }
  char *out = malloc(length+1);
  if (out == NULL) {
    return NULL;
  }
  copy_from_user(out, user_string, length+1);
  return out;
}

void
syscall_init (void) 
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

int
sys_write (int fd, void *data, unsigned data_len) {

  int return_value;
  char *copy = malloc(data_len);
  if (copy == NULL) {
    return_value = -1;
    goto done_nolock;
  }
  if (copy_from_user(copy, data, data_len) == -1) {
    return_value = -1;
    goto done_nolock;
  }

  lock_acquire (&filesys_lock);
	if (fd == STDOUT_FILENO) {
		putbuf (copy, data_len);
    return_value = data_len;
		goto done;
	}

	else if (fd == STDIN_FILENO) {
		return_value = -1;
    goto done;
	}

	else {
    /* unimplemented */
		return_value = -1;
    goto done;
	}

done:
  lock_release(&filesys_lock);
done_nolock:
  free(copy);
  return return_value;
}

int
sys_exec(const char *cmd_line) {
  int return_value;
  char *cmd_line_copy;
  if ((cmd_line_copy = strdup_user(cmd_line)) == NULL) {
    return -1;
  }
  return_value = process_execute(cmd_line_copy);
  free(cmd_line_copy);
  return return_value;
}

int
sys_wait(pid_t pid) {
  return process_wait(pid);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  struct syscall_arguments *args = f->esp;
  access_ok(args, true);
  access_ok(&args->syscall_args[5], true);
  
  switch (args->syscall_nr) {
    case SYS_HALT: {
      shutdown_power_off();
      break;
    }
    case SYS_EXIT: {
      struct process_info *pi = thread_current()->process_info;
      pi->exit_code = args->syscall_args[0];
      printf("%s: exit(%d)\n", pi->file_name, pi->exit_code);
      thread_exit();
      break;
    }
    case SYS_WRITE: {
      f->eax = sys_write((int)args->syscall_args[0], (void *)args->syscall_args[1], args->syscall_args[2]);
      break;
    }
    case SYS_EXEC: {
      f->eax = sys_exec((const char *)args->syscall_args[0]);
      break;
    }
    case SYS_WAIT: {
      f->eax = sys_wait((pid_t)args->syscall_args[0]);
      break;
    }
    default: {
      printf("unimplemented syscall %d\n", args->syscall_nr);
      break;
    }
  }
}
