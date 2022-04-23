#include <string.h>
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"
#include "vm/vpage.h"
#include "filesys/file.h"

#define PAGE_SIZE 0x1000

static struct hash vpage_info_map;
static struct lock vm_lock;

static int vpage_hash(struct hash_elem *);
static bool vpage_less(struct hash_elem *, struct hash_elem *);
static void *evict_page();
static void vpage_info_inmem_to_swap(struct vpage_info *vpi);
static void vpage_info_lazy_to_inmem(struct vpage_info *vpi);
static void vpage_info_swap_to_inmem(struct vpage_info *vpi);

static int vpage_hash(struct hash_elem *e) {
    struct vpage_info *vp = hash_entry(e, struct vpage_info, elem);
    uint32_t v1 = (uint32_t)vp->uaddr;
    uint32_t v2 = vp->pid;
    return v1 * v2;
};

static bool vpage_less(struct hash_elem *e1, struct hash_elem *e2) {
    struct vpage_info *vp1, *vp2;
    vp1 = hash_entry(e1, struct vpage_info, elem);
    vp2 = hash_entry(e2, struct vpage_info, elem);
    if (vp1->uaddr == vp2->uaddr && vp1->pid == vp2->pid) {
        return false;
    }
    else {
        return true;
    }
}

// synchronization must be guaranteed by the caller
static void *evict_page() {
    struct hash_iterator i;
    struct vpage_info *lru_vpi = NULL;
    int swap_idx;
    hash_first(&i, &vpage_info_map);
    while(hash_next(&i)) {
        struct vpage_info *vpi = hash_entry(hash_cur(&i), struct vpage_info, elem);
        if (vpi->status == VPAGE_INMEM) {
            if (lru_vpi) {
                if (vpi->backend.inmem.last_use < lru_vpi->backend.inmem.last_use) {
                    lru_vpi = vpi;
                }
            }
            else {
                lru_vpi = vpi;
            }
        }
    }
    ASSERT(lru_vpi != NULL);
    vpage_info_inmem_to_swap(lru_vpi);
    return lru_vpi->backend.inmem.paddr;
}

// synchronization must be guaranteed by the caller
static void
vpage_info_inmem_to_swap(struct vpage_info *vpi) {
    ASSERT(vpi->status == VPAGE_INMEM);
    uint32_t swap_idx;
    pagedir_clear_page(thread_current()->pagedir, vpi->uaddr);
    swap_idx = swap_out(vpi->backend.inmem.paddr);
    vpi->backend.swap.swap_index = swap_idx;
    vpi->status = VPAGE_SWAPPED;
}

// synchronization must be guaranteed by the caller
static void
vpage_info_lazy_to_inmem(struct vpage_info *vpi) {
    ASSERT(vpi->status == VPAGE_LAZY);
    void *paddr;
    if ((paddr = palloc_get_page(PAL_USER|PAL_ZERO)) == NULL) {
        paddr = evict_page();
    }
    if (vpi->backend.lazy.file) {
        file_read_at(vpi->backend.lazy.file, paddr, vpi->backend.lazy.length, vpi->backend.lazy.offset);
    }
    else {
        memset(paddr, 0, PAGE_SIZE);
    }
    vpi->backend.inmem.paddr = paddr;
    vpi->backend.inmem.last_use = timer_ticks();
    vpi->status = VPAGE_INMEM;
    pagedir_set_page(thread_current()->pagedir, vpi->uaddr, paddr, vpi->writable);
}

static void
vpage_info_swap_to_inmem(struct vpage_info *vpi) {
    ASSERT(vpi->status == VPAGE_SWAPPED);
    void *paddr;
    if ((paddr = palloc_get_page(PAL_USER|PAL_ZERO)) == NULL) {
        paddr = evict_page();
    }
    swap_in(vpi->backend.swap.swap_index, paddr);
    vpi->backend.inmem.paddr = paddr;
    vpi->backend.inmem.last_use = timer_ticks();
    vpi->status = VPAGE_INMEM;
    pagedir_set_page(thread_current()->pagedir, vpi->uaddr, paddr, vpi->writable);
}

void vpage_init() {
    hash_init(&vpage_info_map, vpage_hash, vpage_less, NULL);
    lock_init(&vm_lock);
}

struct vpage_info *
vpage_info_lazy_allocate(void *uaddr, struct file *file, off_t offset, size_t length, pid_t pid, bool writable) {
    struct vpage_info *new = malloc(sizeof(struct vpage_info));
    if (new == NULL) {
        return NULL;
    }
    new->status = VPAGE_LAZY;
    new->uaddr = uaddr;
    new->backend.lazy.file = file;
    new->backend.lazy.offset = offset;
    new->backend.lazy.length = length;
    new->pid = pid;
    new->writable = writable;
    hash_insert(&vpage_info_map, &new->elem);
    return new;
}

struct vpage_info *
vpage_info_inmem_allocate(void *uaddr, void *paddr, pid_t pid, bool writable) {
    struct vpage_info *new = malloc(sizeof(struct vpage_info));
    if (new == NULL) {
        return NULL;
    }
    new->status = VPAGE_INMEM;
    new->uaddr = uaddr;
    new->backend.inmem.paddr = paddr;
    new->backend.inmem.last_use = timer_ticks();
    new->pid = pid;
    new->writable = writable;
    hash_insert(&vpage_info_map, &new->elem);
    return new;
}

struct vpage_info *
vpage_info_swapped_allocate(void *uaddr, uint32_t swap_idx, pid_t pid, bool writable) {
    struct vpage_info *new = malloc(sizeof(struct vpage_info));
    if (new == NULL) {
        return NULL;
    }
    new->status = VPAGE_INMEM;
    new->uaddr = uaddr;
    new->backend.swap.swap_index = swap_idx;
    new->pid = pid;
    new->writable = writable;
    hash_insert(&vpage_info_map, &new->elem);
    return new;
}

void
vpage_info_release(struct vpage_info *vpi) {
    lock_acquire(&vm_lock);
    switch(vpi->status) {
        case VPAGE_INMEM: {
            palloc_free_page(vpi->backend.inmem.paddr);
            break;
        }
        case VPAGE_LAZY: {
            // dont free file, because it is freed in process_exit anyway
            break;
        }
        case VPAGE_SWAPPED: {
            swap_free(vpi->backend.swap.swap_index);
            break;
        }
        default: {
            NOT_REACHED();
            break;
        }
    }
    hash_delete(&vpage_info_map, &vpi->elem);
    free(vpi);
    lock_release(&vm_lock);
}

// argument must be page aligned
enum user_fault_type vpage_handle_user_fault(void *uaddr) {
    
    enum user_fault_type res;
    pid_t pid;
    void *upage = pg_round_down(uaddr);

    // a thread faulted in user context even if it has no userspace... panic!
    if (!thread_current()->process_info) {
        NOT_REACHED();
    }
    pid = thread_current()->process_info->pid;
    lock_acquire(&vm_lock);
    struct hash_iterator i;
    hash_first(&i, &vpage_info_map);
    while(hash_next(&i)) {
        struct vpage_info *vpi = hash_entry(hash_cur(&i), struct vpage_info, elem);
        if (vpi->uaddr == upage && vpi->pid == pid) {
            switch (vpi->status) {
                case VPAGE_INMEM: {
                    // invalid user memory access
                    res = UFAULT_KILL;
                    goto done;
                }
                case VPAGE_LAZY: {
                    vpage_info_lazy_to_inmem(vpi);
                    res = UFAULT_CONTINUE;
                    goto done;
                }
                case VPAGE_SWAPPED: {
                    vpage_info_swap_to_inmem(vpi);
                    res = UFAULT_CONTINUE;
                    goto done;
                }
                default: {
                    NOT_REACHED();
                }
            }
        }
    }
// not present: kill
res = UFAULT_KILL;
done:
    lock_release(&vm_lock);
    return res;
}