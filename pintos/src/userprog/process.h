#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

#define PID_ERROR ((pid_t)-1)
typedef int pid_t;

struct pending_signal {
    int signum;
    struct list_elem elem;
};

struct signal_handler_info {
    int signum;
    void *handler;
    struct list_elem elem;
};

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

    /* related to memory access */
    bool is_critical;

    /* related to fd management */
    struct list user_file_list;

    /* used for synch */
    struct lock lock;

    /* used for denying writes to executables */
    struct file *exe_file;

    /* used for handling signals */
    struct list signal_handler_infos;
    struct lock pending_signals_lock;
    struct list pending_signals;
};

struct process_start_args {
    char cmd_line[0x800];
    struct semaphore *sema;
    struct process_info *parent_pi;
    struct process_info **out_pi;
};

void process_init();

struct signal_handler_info *signal_handler_info_allocate(int signum, void *handler);
void signal_handler_info_release(struct signal_handler_info * shi);
struct signal_handler_info *get_signal_handler_info(struct process_info *pi, int signum);

struct pending_signal *pending_signal_allocate(int signum);
void pending_signal_release(struct pending_signal *ps);
void pending_signal_handle(struct process_info *pi, struct pending_signal *ps);

pid_t pid_allocate();
void pid_release(pid_t pid);
struct process_info *get_process_info(pid_t pid);

struct process_info *process_info_allocate(struct semaphore *sema, struct process_info *parent_pi);
void process_info_release(struct process_info *info);
void process_info_set_exit_code(struct process_info *info, int exit_code);

pid_t process_execute (const char *file_name);
int process_wait (pid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
