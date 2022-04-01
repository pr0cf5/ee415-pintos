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
bool append_dir(struct process_info *pi, struct dir *dir);
bool append_file(struct process_info *pi, struct file *file);
struct user_file *get_user_file(struct process_info *pi, int fd);
bool remove_user_file(struct process_info *pi, int fd);

void syscall_init (void);
int sys_write(int fd, void *data, unsigned data_len);
int sys_read(int fd, void *data, unsigned int data_len);
int sys_exec(const char *cmd_line);
int sys_wait(pid_t pid);



#endif /* userprog/syscall.h */
