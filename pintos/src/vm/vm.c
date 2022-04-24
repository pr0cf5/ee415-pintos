#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "vm/vm.h"
#include "vm/vpage.h"
#include "vm/swap.h"

void vm_init() {
    vpage_init();
    swap_init();
}

void vm_handle_user_fault(void *uaddr, struct intr_frame *f) {
    // handle stack expansion case
    #define STACK_EXPANSION_RNG 0x100
    #define STACK_MAX_GROWTH_PAGES 0x20
    uint8_t *stack_ptr = f->esp, *fault_ptr = uaddr, *cur_page, *end_page;
    struct vpage_info *new_vpis[STACK_MAX_GROWTH_PAGES] = {0,};
    int i = 0;
    pid_t pid = thread_current()->process_info->pid;
    if (stack_ptr - STACK_EXPANSION_RNG <= fault_ptr && fault_ptr <= stack_ptr + STACK_EXPANSION_RNG) {
        cur_page = pg_round_down(fault_ptr);
        end_page = (uint8_t *)(PHYS_BASE - PGSIZE);
        i = 0;
        while(cur_page < end_page) {
            if (i >= STACK_MAX_GROWTH_PAGES) {
                goto oom;
            }
            if (vpage_info_find(cur_page, pid)) {
                cur_page += PGSIZE;
                continue;
            }
            else {
                if (!(new_vpis[i] = vpage_info_lazy_allocate(cur_page, NULL, 0, 0, pid, true))) {
                    for (int j = 0; j < i; j++) {
                        if (new_vpis[j]) {
                            vpage_info_release(new_vpis[j]);
                        }   
                    }
                    goto oom;
                }
            }
            i++;
            cur_page += PGSIZE;
        }   
    }
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
    return;
oom:
    sys_exit(-1);
}
