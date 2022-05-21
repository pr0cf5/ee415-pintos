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
#include "vm/vpage.h"

static void syscall_handler (struct intr_frame *);
static mid_t mid_allocate();
static void mid_release(mid_t mid);
static bool user_file_less(const struct list_elem *e1, const struct list_elem *e2, void *aux);
static bool append_dir(struct process_info *pi, struct dir *dir, int *fd);
static bool append_file(struct process_info *pi, struct file *file, int *fd);
static struct user_file *user_file_get(struct process_info *pi, int fd);
static bool user_file_get_and_release(struct process_info *pi, int fd);
static int fd_allocate(struct process_info *pi);
static char *strdup_user(const char *user_string, bool *fault);
static struct mmap_entry *mmap_entry_append(struct file *file, void *data);
static struct mmap_entry *mmap_entry_get(mid_t mid);

static mid_t mid_allocate() {
  return ++thread_current()->mid_counter;
}

static void mid_release(mid_t mid) {
  
}

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
static bool append_dir(struct process_info *pi, struct dir *dir, int *fd) {
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
static bool append_file(struct process_info *pi, struct file *file, int *fd) {
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

static struct user_file *user_file_get(struct process_info *pi, int fd) {
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

static bool user_file_get_and_release(struct process_info *pi, int fd) {
  struct user_file *f = user_file_get(pi, fd);
  if (f == NULL) {
    return false;
  }
  list_remove(&f->elem);
  user_file_release(f);
}

static int fd_allocate(struct process_info *pi) {
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

static struct mmap_entry *mmap_entry_append(struct file *file, void *upage) {
  struct mmap_entry *me = malloc(sizeof(struct mmap_entry));
  struct list *me_list = &thread_current()->process_info->mmap_entries_list;
  size_t cur_length, rem_length;
  off_t cur_offset;
  if (me == NULL) {
    return NULL;
  }
  me->file = file;
  me->mid = mid_allocate();
  me->length = file_length(file);
  me->page_cnt = (size_t)pg_round_up(me->length) / PGSIZE;
  me->uaddr = upage;
  me->dirty = false;
  // what about the executable file, can this be mapped as writable? hmmm...
  rem_length = me->length;
  cur_offset = 0;
  for (int i = 0; i < me->page_cnt; i++) {
    cur_length = rem_length > PGSIZE ? PGSIZE : rem_length;
    struct file *file_copy = file_reopen(file);
    // initially, set permission to read-only
    if (!file_copy || 
      !vpage_info_lazy_allocate((char *)upage + PGSIZE*i, file_copy, cur_offset, cur_length, thread_current()->process_info->pid, false)) 
      {
        free(me);
        for (int j = 0; j < i; j++) {
          vpage_info_find_and_release((char *)upage + PGSIZE*j, thread_current()->process_info->pid);
      }
      return NULL;
    }
    rem_length -= cur_length; 
    cur_offset += PGSIZE;
  }
  
  list_push_back(me_list, &me->elem);
  return me;
}

static struct mmap_entry *mmap_entry_get(mid_t mid) {
  struct list *me_list = &thread_current()->process_info->mmap_entries_list;
  for (struct list_elem *e = list_begin(me_list); e != list_end(me_list); e = list_next(e)) {
    struct mmap_entry *me = list_entry(e, struct mmap_entry, elem);
    if (me->mid == mid) {
      return me;
    }
  }
  return NULL;
}

struct mmap_entry *mmap_entry_get_by_addr(void *uaddr) {
  struct list *me_list = &thread_current()->process_info->mmap_entries_list;
  for (struct list_elem *e = list_begin(me_list); e != list_end(me_list); e = list_next(e)) {
    struct mmap_entry *me = list_entry(e, struct mmap_entry, elem);
    if ((char *)me->uaddr <= uaddr && uaddr < (char *)me->uaddr + me->length) {
      return me;
    }
  }
  return NULL;
}

void mmap_entry_release(struct mmap_entry *me) {
  struct vpage_info *vpi = NULL;
  int i;
  // optimize: if vpi is lazy, don't do writeback
  // this need not be locked because transition from lazy to inmem can only be triggered by the current process
  for (i = 0; i < me->page_cnt; i++) {
    struct vpage_info *vpi_ = NULL;
    if ((vpi_ = vpage_info_find((char*)me->uaddr + PGSIZE*i, thread_current()->process_info->pid)) == NULL) {
      NOT_REACHED();
    }
    if (vpi_->status != VPAGE_LAZY) {
      break;
    }
  }
  if (me->dirty) {
    fault_region_enter();
    file_write(me->file, me->uaddr, me->length);
    fault_region_exit();
  }
  
  // close file
  file_close(me->file);

  // release vpage_info
  for (int i = 0; i < me->page_cnt; i++) {
    vpage_info_find_and_release((char*)me->uaddr + PGSIZE*i, thread_current()->process_info->pid);
  }
  list_remove(&me->elem);  
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
  file = filesys_open(copy);
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
    if (!user_file_get_and_release(thread_current()->process_info, fd)) {
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
  success = filesys_create(copy, initial_size, false);
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
  success = filesys_remove(copy);
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
      for (int i = 0; i < data_len; i++) {
        copy[i] = input_getc();
      }
      return_value = data_len;
      goto done;
    }
    case UserFileStdout: {
      return_value = -1;
      goto done;
    }
    case UserFileFile: {
      return_value = file_read(f->inner.file, copy, data_len);
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
      putbuf (copy, data_len);
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
      return_value = file_write(f->inner.file, copy, data_len);
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
    file_seek(file->inner.file, position);
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
    return_value = file_tell(file->inner.file);
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
    return_value = file_length(file->inner.file);
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

int sys_sendsig(pid_t pid, int signum) {
  struct process_info *pi = get_process_info(pid);
  struct pending_signal *ps = pending_signal_allocate(signum);
  if (pi == NULL || ps == NULL) {
    return -1;
  }
  lock_acquire(&pi->pending_signals_lock);
  list_push_back(&pi->pending_signals, &ps->elem);
  lock_release(&pi->pending_signals_lock);
  return 0;
}

int sys_sigaction(int signum, void *handler) {
  struct signal_handler_info *shi;
  struct process_info *pi = thread_current()->process_info;
  if ((shi = get_signal_handler_info(pi, signum)) != NULL) {
    shi->handler = handler;
  }
  else {
    shi = signal_handler_info_allocate(signum, handler);
    if (shi == NULL) {
      return -1;
    }
    list_push_back(&pi->signal_handler_infos, &shi->elem);
  }
  return 0;
}
     
int sys_yield() {
  thread_yield();
  return 0;
}

mid_t sys_mmap(int fd, void *data) {
  mid_t mid;
  struct user_file *file;
  struct file *mmap_file;
  
  if (pg_round_down(data) == NULL) {
    mid = -1;
    goto done;
  }

  if ((file = user_file_get(thread_current()->process_info, fd)) == NULL) {
    mid = -1;
    goto done;
  }
  if (file->type == UserFileFile) {
    mmap_file = file_reopen(file->inner.file);
    void *upage = pg_round_down(data);
    if (upage != data) {
      mid = -1;
      goto done;
    }
    struct mmap_entry *me = mmap_entry_append(mmap_file, upage);
    if (me == NULL) {
      mid = -1;
      goto done;
    }
    mid = me->mid;
  }
  else {
    mid = -1;
    goto done;
  } 
done:
  return mid;
}

int sys_munmap(mid_t mid) {
  struct mmap_entry *me = mmap_entry_get(mid);
  mmap_entry_release(me);
}

void sys_exit(int exit_code) {
  struct process_info *pi = thread_current()->process_info;
  pi->exit_code = exit_code;
  printf("%s: exit(%d)\n", pi->file_name, pi->exit_code);
  thread_exit();
}

int sys_chdir(const char *path) {
  bool fault, success;
  int return_value, fd;
  struct file *file;
  struct canon_path *cpath;
  struct dir *dir, *old_dir;
  char *copy, *name;
  struct inode *inode;
  copy = strdup_user(path, &fault);
  if (fault) {
    sys_exit(-1);
  }
  if (copy == NULL) {
    return_value = 0;
    goto done_nocopy;
  }
  if (!path_canonicalize(path, &cpath)) {
    return_value = 1;
    goto done;
  }
  name = canon_path_get_leaf(cpath);
  old_dir = thread_current()->process_info->cwd;
  if (!(dir = dir_open_canon_path(cpath, true))) {
    canon_path_release(cpath);
    thread_current()->process_info->cwd = old_dir;
    return_value = 0;
    goto done;
  }
  thread_current()->process_info->cwd = dir;
  dir_close(old_dir);
  canon_path_release(cpath);
done:
  free(copy);
done_nocopy:
  return return_value;
}

int sys_mkdir(const char *path) {
  bool fault, success;
  int return_value, fd;
  struct file *file;
  char *copy = strdup_user(path, &fault);
  if (fault) {
    sys_exit(-1);
  }
  if (copy == NULL) {
    return_value = 0;
    goto done_nocopy;
  }
  success = filesys_create(copy, 0, true);
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

int sys_readdir(const char *path) {
  return 0;
}

int sys_isdir(int fd) {
  return 0;
}

int sys_inumber(int fd) {
  return 0;
}

static void
syscall_handler (struct intr_frame *f) 
{
  struct syscall_arguments *args = f->esp;
  struct syscall_arguments args_copy;
  if (!access_ok(args, true) || !access_ok((char *)args + sizeof(struct syscall_arguments), true)) {
    sys_exit(-1);
    return;
  }
  fault_region_enter();
  memcpy(&args_copy, args, sizeof(args_copy));
  fault_region_exit();

  switch (args_copy.syscall_nr) {
    case SYS_HALT: {
      shutdown_power_off();
      break;
    }
    case SYS_EXIT: {
      sys_exit((int)args_copy.syscall_args[0]);
      break;
    }
    case SYS_OPEN: {
      f->eax = sys_open((const char *)args_copy.syscall_args[0]);
      break;
    }
    case SYS_CREATE: {
      f->eax = sys_create((const char *)args_copy.syscall_args[0], (unsigned)args_copy.syscall_args[1]);
      break;
    }
    case SYS_REMOVE: {
      f->eax = sys_remove((const char *)args_copy.syscall_args[0]);
    }
    case SYS_CLOSE: {
      f->eax = sys_close((int)args_copy.syscall_args[0]);
      break;
    }
    case SYS_WRITE: {
      f->eax = sys_write((int)args_copy.syscall_args[0], (void *)args_copy.syscall_args[1], args_copy.syscall_args[2]);
      break;
    }
    case SYS_READ: {
      f->eax = sys_read((int)args_copy.syscall_args[0], (void *)args_copy.syscall_args[1], args_copy.syscall_args[2]);
      break;
    }
    case SYS_SEEK: {
      f->eax = sys_seek((int)args_copy.syscall_args[0], (int)args_copy.syscall_args[1]);
      break;
    }
    case SYS_TELL: {
      f->eax = sys_filesize((int)args_copy.syscall_args[0]);
      break;
    }
    case SYS_FILESIZE: {
      f->eax = sys_filesize((int)args_copy.syscall_args[0]);
      break;
    }
    case SYS_EXEC: {
      f->eax = sys_exec((const char *)args_copy.syscall_args[0]);
      break;
    }
    case SYS_WAIT: {
      f->eax = sys_wait((pid_t)args_copy.syscall_args[0]);
      break;
    }
    case SYS_SIGACTION: {
      f->eax = sys_sigaction((int)args_copy.syscall_args[0], (void *)args_copy.syscall_args[1]);
      break;
    }
    case SYS_SENDSIG: {
      f->eax = sys_sendsig((pid_t)args_copy.syscall_args[0], (int)args_copy.syscall_args[1]);
      break;
    }
    case SYS_YIELD: {
      f->eax = sys_yield();
      break;
    }
    case SYS_MMAP: {
      f->eax = sys_mmap((int)args_copy.syscall_args[0], (void *)args_copy.syscall_args[1]);
      break;
    }
    case SYS_MUNMAP: {
      f->eax = sys_munmap((mid_t)args_copy.syscall_args[0]);
      break;
    }
    case SYS_CHDIR: {
      f->eax = sys_chdir((const char *)args_copy.syscall_args[0]);
      break;
    }
    case SYS_MKDIR: {
      f->eax = sys_mkdir((const char *)args_copy.syscall_args[0]);
      break;
    }
    case SYS_READDIR: {
      f->eax = sys_readdir((const char *)args_copy.syscall_args[0]);
      break;
    }
    case SYS_ISDIR: {
      f->eax = sys_isdir((int)args_copy.syscall_args[0]);
      break;
    }
    case SYS_INUMBER: {
      f->eax = sys_inumber((int)args_copy.syscall_args[0]);
      break;
    }
    default: {
      printf("unimplemented syscall %d\n", args_copy.syscall_nr);
      break;
    }
  }
}
