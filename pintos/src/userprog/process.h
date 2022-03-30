#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

#define PID_ERROR ((pid_t)-1)
typedef int pid_t;

struct pid_allocator {
    struct lock pid_lock;
    pid_t last_pid;
};

enum process_status {
    PROCESS_RUNNING,
    PROCESS_EXITED,
};

struct process_info {
    pid_t pid;
    char file_name[20];
    struct thread *thread;

    /* warning: this is only used in child side. this is because parents can have multiple children */
    struct semaphore *sema;

    /* used for wait */
    enum process_status status;
    int exit_code;
    
    /* used to create tree */
    struct process_info *parent_pi;
    struct list children_pi;
    struct list_elem elem;
};

struct process_start_args {
    char cmd_line[0x800];
    struct semaphore *sema;
    struct process_info *parent_pi;
};

void process_init();

pid_t pid_allocate();
void pid_release(pid_t pid);

struct process_info *process_info_allocate(struct semaphore *sema, struct process_info *parent_pi);
void process_info_release(struct process_info *info);

pid_t process_execute (const char *file_name);
int process_wait (pid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
