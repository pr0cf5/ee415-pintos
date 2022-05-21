#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "process.h"
#include <stdint.h>

struct syscall_arguments {
    uint32_t syscall_nr;
    uint32_t syscall_args[5];
};

#define STDIN_FILENO 0
#define STDOUT_FILENO 1

enum UserFileType {
    UserFileStdin,
    UserFileStdout,
    UserFileFile,
    UserFileDir,
};

struct user_file {
    int fd;
    enum UserFileType type;
    union {
        struct file *file;
        struct dir *dir;
    } inner;
    struct list_elem elem;
};

typedef int mid_t;

struct mmap_entry {
    mid_t mid;
    struct file *file;
    void *uaddr;
    size_t length;
    size_t page_cnt;
    bool dirty;
    struct list_elem elem;
};

bool init_stdin(struct process_info *pi);
bool init_stdout(struct process_info *pi);
void user_file_release(struct user_file *uf);

void syscall_init (void);
int sys_create(const char *file_name, size_t initial_size);
int sys_remove(const char *file_name);
int sys_open(const char *file_name);
int sys_write(int fd, void *data, unsigned data_len);
int sys_read(int fd, void *data, unsigned int data_len);
int sys_seek(int fd, unsigned position);
int sys_tell(int fd);
int sys_filesize(int fd);
int sys_exec(const char *cmd_line);
int sys_wait(pid_t pid);
int sys_sendsig(pid_t pid, int signum);
int sys_sigaction(int signum, void *handler);
int sys_yield();
mid_t sys_mmap(int fd, void *data);
int sys_munmap(mid_t mid);
void sys_exit(int exit_code);
int sys_chdir(const char *path);
int sys_mkdir(const char *path);
int sys_readdir(int fd, char *name);
int sys_isdir(int fd);
int sys_inumber(int fd);
void mmap_entry_allocate();
struct mmap_entry *mmap_entry_get_by_addr(void *uaddr);
void mmap_entry_release(struct mmap_entry *);


#endif /* userprog/syscall.h */
