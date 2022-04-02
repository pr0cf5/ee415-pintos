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
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/directory.h"
#include "devices/input.h"

struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);

static bool user_file_less(const struct list_elem *e1, const struct list_elem *e2, void *aux) {
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
bool append_dir(struct process_info *pi, struct dir *dir, int *fd) {
  struct user_file *f = malloc(sizeof(struct user_file));
  if (f == NULL) {
    return false;
  }
  f->fd = fd_allocate(pi);
  f->type = UserFileDir;
  f->inner.dir = dir;
  list_insert_ordered(&pi->user_file_list, &f->elem, user_file_less, NULL);
  *fd = f->fd;
  return true;
}

/* allocates fd and user_file structure for file, and appends it to pi's list */
bool append_file(struct process_info *pi, struct file *file, int *fd) {
  struct user_file *f = malloc(sizeof(struct user_file));
  if (f == NULL) {
    return false;
  }
  f->fd = fd_allocate(pi);
  f->type = UserFileFile;
  f->inner.file = file;
  list_insert_ordered(&pi->user_file_list, &f->elem, user_file_less, NULL);
  *fd = f->fd;
  return true;
}

struct user_file *user_file_get(struct process_info *pi, int fd) {
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

bool user_file_remove(struct process_info *pi, int fd) {
  struct user_file *f = user_file_get(pi, fd);
  if (f == NULL) {
    return false;
  }
  list_remove(&f->elem);
  if (f->type == UserFileFile || f->type == UserFileDir) {
    lock_acquire(&filesys_lock);
    file_close(f->inner.file);
    lock_release(&filesys_lock);
  }
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

void user_file_release(struct user_file *uf) {
  switch (uf->type) {
    case UserFileDir: {
      dir_close(uf->inner.file);
    }
    case UserFileFile: {
      file_close(uf->inner.file);
    }
    default: {
      break;
    }
  }
  free(uf);
}


static char *strdup_user(const char *user_string, bool *fault) {
  size_t length = 0;
  *fault = false;
  for (size_t i = 0 ;; i++) {
    char c;
    if (copy_from_user(&c, user_string+i, 1) != 1) {
      *fault = true;
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
  if (copy_from_user(out, user_string, length+1) == -1) {
    *fault = true;
    free(out);
    return NULL;
  }
  return out;
}

void
syscall_init (void) 
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

int
sys_open(const char *file_name) {
  bool fault;
  int return_value, fd;
  struct file *file;
  char *copy = strdup_user(file_name, &fault);
  if (copy == NULL) {
    if (fault) {
      sys_exit(-1);
    }
    return_value = -1;
    goto done_nocopy;
  }
  lock_acquire(&filesys_lock);
  file = filesys_open(file_name);
  lock_release(&filesys_lock);
  if (file == NULL) {
    return_value = -1;
    goto done;
  }
  
  if (!append_file(thread_current()->process_info, file, &fd)) {
    return_value = -1;
    goto done;
  }
  return_value = fd;
done:
  free(copy);
done_nocopy:
  return return_value;
}

int
sys_close(int fd) {
  int return_value;
  struct user_file *file;
  if ((file = user_file_get(thread_current()->process_info, fd)) == NULL) {
    return_value = -1;
    goto done;
  }
  if (file->type == UserFileFile || file->type == UserFileDir) {
    if (!user_file_remove(thread_current()->process_info, fd)) {
      return_value = -1;
      goto done;
    }
    return_value = 0;
    goto done;

  }
  else {
    return_value = -1;
    goto done;
  } 
done:
  return return_value;
}

int
sys_create(const char *file_name, size_t initial_size) {
  bool fault, success;
  int return_value, fd;
  struct file *file;
  char *copy = strdup_user(file_name, &fault);
  if (fault) {
    sys_exit(-1);
  }
  if (copy == NULL) {
    return_value = 0;
    goto done_nocopy;
  }
  lock_acquire(&filesys_lock);
  success = filesys_create(file_name, initial_size);
  lock_release(&filesys_lock);
  if (!success) {
    return_value = 0;
    goto done;
  }
  if (!append_file(thread_current()->process_info, file, &fd)) {
    return_value = 0;
    goto done;
  }
  return_value = 1;
done:
  free(copy);
done_nocopy:
  return return_value;
}

int
sys_remove(const char *file_name) {
  bool fault, success;
  int return_value;
  struct file *file;
  char *copy = strdup_user(file_name, &fault);
  if (copy == NULL) {
    if (fault) {
      sys_exit(-1);
    }
    return_value = 0;
    goto done_nocopy;
  }
  lock_acquire(&filesys_lock);
  success = filesys_remove(file_name);
  lock_release(&filesys_lock);
  if (!success) {
    return_value = 0;
    goto done;
  }
  return_value = 1;
done:
  free(copy);
done_nocopy:
  return return_value;
}

int
sys_read(int fd, void *data, unsigned data_len) {
  int return_value;
  if (data_len == 0) {
    return_value = 0;
    goto done_nocopy;
  }
  char *copy = malloc(data_len);
  if (copy == NULL) {
    return_value = -1;
    goto done_nocopy;
  }
  struct user_file *f = user_file_get(thread_current()->process_info, fd);
  if (f == NULL || f->type == UserFileDir) {
    return_value = -1;
    goto done;
  }
  switch (f->type) {
    case UserFileStdin: {
      lock_acquire(&filesys_lock);
      for (int i = 0; i < data_len; i++) {
        copy[i] = input_getc();
      }
      lock_release(&filesys_lock);
      return_value = data_len;
      goto done;
    }
    case UserFileStdout: {
      return_value = -1;
      goto done;
    }
    case UserFileFile: {
      lock_acquire(&filesys_lock);
      return_value = file_read(f->inner.file, copy, data_len);
      lock_release(&filesys_lock);
      if (return_value == -1) {
        goto done;
      }
      if (copy_to_user(data, copy, return_value) == -1) {
        sys_exit(-1);
      }
      goto done;
      break;
    }
    case UserFileDir: {
      return_value = -1;
      goto done;
      break;
    }
    default: {
      ASSERT(0);
      goto done;
      break;
    }
  }

done:
  free(copy);
done_nocopy:
  return return_value;
}

int
sys_write (int fd, void *data, unsigned data_len) {
  int return_value;
  if (data_len == 0) {
    return_value = 0;
    goto done_nocopy;
  }
  char *copy = malloc(data_len);
  if (copy == NULL) {
    return_value = -1;
    goto done_nocopy;
  }
  if (copy_from_user(copy, data, data_len) == -1) {
    sys_exit(-1);
  }

  struct user_file *f = user_file_get(thread_current()->process_info, fd);
  if (f == NULL || f->type == UserFileDir) {
    goto done;
  }
  switch (f->type) {
    case UserFileStdout: {
      lock_acquire(&filesys_lock);
      putbuf (copy, data_len);
      lock_release(&filesys_lock);
      return_value = data_len;
      goto done;
      break;
    }
    case UserFileStdin: {
      return_value = -1;
      goto done;
      break;
    }
    case UserFileFile: {
      lock_acquire(&filesys_lock);
      return_value = file_write(f->inner.file, copy, data_len);
      lock_release(&filesys_lock);
      goto done;
      break;
    }
    case UserFileDir: {
      return_value = -1;
      goto done;
      break;
    }
    default: {
      ASSERT(0);
    }
  }
done:
  free(copy);
done_nocopy:
  return return_value;
}

int
sys_seek(int fd, unsigned position) {
  int return_value;
  struct user_file *file;
  if ((file = user_file_get(thread_current()->process_info, fd)) == NULL) {
    return_value = -1;
    goto done;
  }
  if (file->type == UserFileFile) {
    lock_acquire(&filesys_lock);
    file_seek(file->inner.file, position);
    lock_release(&filesys_lock);
    return_value = 0;
    goto done;
  }
  else {
    return_value = -1;
    goto done;
  } 
done:
  return return_value;
}

int
sys_tell(int fd) {
  int return_value;
  struct user_file *file;
  
  if ((file = user_file_get(thread_current()->process_info, fd)) == NULL) {
    return_value = -1;
    goto done;
  }
  if (file->type == UserFileFile) {
    lock_acquire(&filesys_lock);
    return_value = file_tell(file->inner.file);
    lock_release(&filesys_lock);
    goto done;

  }
  else {
    return_value = -1;
    goto done;
  } 
done:
  return return_value;
}

int
sys_filesize(int fd) {
  int return_value;
  struct user_file *file;
  
  if ((file = user_file_get(thread_current()->process_info, fd)) == NULL) {
    return_value = -1;
    goto done;
  }
  if (file->type == UserFileFile) {
    lock_acquire(&filesys_lock);
    return_value = file_length(file->inner.file);
    lock_release(&filesys_lock);
    goto done;

  }
  else {
    return_value = -1;
    goto done;
  } 
done:
  return return_value;
}

int
sys_exec(const char *cmd_line) {
  bool fault;
  int return_value;
  char *cmd_line_copy;
  if ((cmd_line_copy = strdup_user(cmd_line, &fault)) == NULL) {
    if (fault) {
      sys_exit(-1);
    }
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

void sys_exit(int exit_code) {
  struct process_info *pi = thread_current()->process_info;
  pi->exit_code = exit_code;
  printf("%s: exit(%d)\n", pi->file_name, pi->exit_code);
  thread_exit();
}

void
syscall_handler (struct intr_frame *f) 
{
  struct syscall_arguments *args = f->esp;
  if (!access_ok(args, true) || !access_ok(&args->syscall_args[5], true)) {
    sys_exit(-1);
    return;
  }
  
  switch (args->syscall_nr) {
    case SYS_HALT: {
      shutdown_power_off();
      break;
    }
    case SYS_EXIT: {
      sys_exit((int)args->syscall_args[0]);
      break;
    }
    case SYS_OPEN: {
      f->eax = sys_open((char *)args->syscall_args[0]);
      break;
    }
    case SYS_CREATE: {
      f->eax = sys_create((char *)args->syscall_args[0], (unsigned)args->syscall_args[1]);
      break;
    }
    case SYS_REMOVE: {
      f->eax = sys_remove((char *)args->syscall_args[0]);
    }
    case SYS_CLOSE: {
      f->eax = sys_close((int)args->syscall_args[0]);
      break;
    }
    case SYS_WRITE: {
      f->eax = sys_write((int)args->syscall_args[0], (void *)args->syscall_args[1], args->syscall_args[2]);
      break;
    }
    case SYS_READ: {
      f->eax = sys_read((int)args->syscall_args[0], (void *)args->syscall_args[1], args->syscall_args[2]);
      break;
    }
    case SYS_SEEK: {
      f->eax = sys_seek((int)args->syscall_args[0], (int)args->syscall_args[1]);
      break;
    }
    case SYS_TELL: {
      f->eax = sys_filesize((int)args->syscall_args[0]);
      break;
    }
    case SYS_FILESIZE: {
      f->eax = sys_filesize((int)args->syscall_args[0]);
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
