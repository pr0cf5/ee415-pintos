#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "vm/vm.h"
#include "vm/vpage.h"
#include "vm/swap.h"

void vm_init() {
    vpage_init();
    swap_init();
}

void vm_handle_user_fault(void *uaddr) {
    enum user_fault_type ty = vpage_handle_user_fault(uaddr);
    switch (ty) {
        case UFAULT_KILL: {
            sys_exit(-1);
        }
        case UFAULT_CONTINUE: {
            break;
        }
        default: {
            NOT_REACHED();
            break;
        }
    }
}
