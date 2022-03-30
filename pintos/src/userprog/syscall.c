#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "devices/shutdown.h"

static struct lock stdout_lock;

static void syscall_handler (struct intr_frame *);

static bool access_ok(void *_userptr) {
  uint32_t userptr = (uint32_t)_userptr;
  if (userptr > PHYS_BASE) {
    return false;
  }
  return true;
}

void
syscall_init (void) 
{
  lock_init(&stdout_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

int
sys_write (int fd, void *data, unsigned data_len) {

  access_ok(data);
	access_ok((char *)data + data_len);

	if (fd == STDOUT_FILENO) {
		lock_acquire (&stdout_lock);
		putbuf (data, data_len);
		lock_release (&stdout_lock);
		return data_len;
	}

	else if (fd == STDIN_FILENO) {
		return -1;
	}

	else {
    /* unimplemented */
		return -1;
	}
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  struct syscall_arguments *args = f->esp;
  access_ok(args);
  
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
    default: {
      printf("unimplemented syscall %d\n", args->syscall_nr);
      break;
    }
  }
}
