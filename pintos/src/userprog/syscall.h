#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdint.h>

struct syscall_arguments {
    uint32_t syscall_nr;
    uint32_t syscall_args[];
};

#define STDIN_FILENO 0
#define STDOUT_FILENO 1

void syscall_init (void);
int sys_write(int fd, void *data, unsigned data_len);
int sys_read(int fd, void *data, unsigned int data_len);

#endif /* userprog/syscall.h */
