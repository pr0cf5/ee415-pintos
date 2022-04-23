#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "process.h"
#include <stdint.h>

struct syscall_arguments {
    uint32_t syscall_nr;
    uint32_t syscall_args[];
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

/* user_file related APIs */
int fd_allocate(struct process_info *pi);
int fd_release();
bool init_stdin(struct process_info *pi);
bool init_stdout(struct process_info *pi);
bool append_dir(struct process_info *pi, struct dir *dir, int *fd);
bool append_file(struct process_info *pi, struct file *file, int *fd);
struct user_file *user_file_get(struct process_info *pi, int fd);
void user_file_release(struct user_file *uf);
bool user_file_remove(struct process_info *pi, int fd);

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
void sys_exit(int exit_code);


#endif /* userprog/syscall.h */
