#include <string.h>
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "userprog/exception.h"
#include "userprog/syscall.h"
#include "vm/swap.h"
#include "vm/vpage.h"
#include "filesys/file.h"

static struct hash vpage_info_map;
static struct lock vm_lock;

static int vpage_hash(struct hash_elem *);
static bool vpage_less(struct hash_elem *, struct hash_elem *);
static void *evict_page();
static void vpage_info_inmem_to_swap(struct vpage_info *vpi);
static void vpage_info_lazy_to_inmem(struct vpage_info *vpi);
static void vpage_info_swap_to_inmem(struct vpage_info *vpi);
void vpage_info_release_inner(struct vpage_info *vpi);

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
    void *paddr;
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
    paddr = lru_vpi->backend.inmem.paddr;
    vpage_info_inmem_to_swap(lru_vpi);
    return paddr;
}

// synchronization must be guaranteed by the caller
static void
vpage_info_inmem_to_swap(struct vpage_info *vpi) {
    ASSERT(vpi->status == VPAGE_INMEM);
    uint32_t swap_idx;
    pagedir_clear_page(vpi->backend.inmem.pagedir, vpi->uaddr);
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
        memset((char *)paddr + vpi->backend.lazy.length, 0, PGSIZE - vpi->backend.lazy.length);
        file_close(vpi->backend.lazy.file);
    }
    else {
        memset(paddr, 0, PGSIZE);
    }
    vpi->backend.inmem.paddr = paddr;
    vpi->backend.inmem.pagedir = thread_current()->pagedir;
    vpi->backend.inmem.last_use = timer_ticks();
    vpi->status = VPAGE_INMEM;
    pagedir_set_page(vpi->backend.inmem.pagedir, vpi->uaddr, paddr, vpi->writable);
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
    vpi->backend.inmem.pagedir = thread_current()->pagedir;
    vpi->backend.inmem.last_use = timer_ticks();
    vpi->status = VPAGE_INMEM;
    pagedir_set_page(vpi->backend.inmem.pagedir, vpi->uaddr, paddr, vpi->writable);
}

// synchronization must be guaranteed by the caller
void vpage_info_release_inner(struct vpage_info *vpi) {
    switch (vpi->status) {
        case VPAGE_INMEM: {
            pagedir_clear_page(vpi->backend.inmem.pagedir, vpi->uaddr);
            palloc_free_page(vpi->backend.inmem.paddr);
            hash_delete(&vpage_info_map, &vpi->elem);
            free(vpi);
            break;
        }
        case VPAGE_LAZY: {
            if (vpi->backend.lazy.file != NULL) {
                file_close(vpi->backend.lazy.file);
                vpi->backend.lazy.file = NULL;
            }
            hash_delete(&vpage_info_map, &vpi->elem);
            free(vpi);
            break;
        }
        case VPAGE_SWAPPED: {
            swap_free(vpi->backend.swap.swap_index);
            hash_delete(&vpage_info_map, &vpi->elem);
            free(vpi);
            break;
        }
        default: {
            NOT_REACHED();
        }
    }
}

void vpage_init() {
    hash_init(&vpage_info_map, vpage_hash, vpage_less, NULL);
    lock_init(&vm_lock);
}

struct vpage_info *
vpage_info_lazy_allocate(void *uaddr, struct file *file, off_t offset, size_t length, pid_t pid, bool writable) {
    struct vpage_info *new = malloc(sizeof(struct vpage_info)), *old;
    struct file *file_copy;

    if (!new) {
        return NULL;
    }

    if (file != NULL) {
        file_copy = file_reopen(file);
        if (!file_copy) {
            return NULL;
        }
    }
    else {
        file_copy = NULL;
    }
    
    new->status = VPAGE_LAZY;
    new->uaddr = uaddr;
    new->backend.lazy.file = file_copy;
    new->backend.lazy.offset = offset;
    new->backend.lazy.length = length;
    new->pid = pid;
    new->writable = writable;
    lock_acquire(&vm_lock);
    if ((old = hash_find(&vpage_info_map, &new->elem)) != NULL) {
        free(new);
        new = NULL;
        goto done;
    }
    hash_insert(&vpage_info_map, &new->elem);
done:
    lock_release(&vm_lock);
    return new;
}

// alloates paddr for you
struct vpage_info *
vpage_info_inmem_allocate(void *uaddr, void **paddr_, pid_t pid, bool writable) {
    void *paddr;
    struct vpage_info *new = malloc(sizeof(struct vpage_info)), *old;
    if (new == NULL) {
        return NULL;
    }
    lock_acquire(&vm_lock);
    if ((paddr = palloc_get_page(PAL_USER|PAL_ZERO)) == NULL) {
        paddr = evict_page();
    }
    new->status = VPAGE_INMEM;
    new->uaddr = uaddr;
    new->backend.inmem.paddr = paddr;
    new->backend.inmem.pagedir = thread_current()->pagedir;
    new->backend.inmem.last_use = timer_ticks();
    new->pid = pid;
    new->writable = writable;
    
    if ((old = hash_find(&vpage_info_map, &new->elem)) != NULL) {
        NOT_REACHED();
        free(new);
        new = NULL;
        goto done;
    }
    hash_insert(&vpage_info_map, &new->elem);
    pagedir_set_page(new->backend.inmem.pagedir, uaddr, paddr, writable);
    *paddr_ = paddr;
done:
    lock_release(&vm_lock);
    return new;
}

struct vpage_info *
vpage_info_swapped_allocate(void *uaddr, uint32_t swap_idx, pid_t pid, bool writable) {
    struct vpage_info *new = malloc(sizeof(struct vpage_info)), *old;
    if (new == NULL) {
        return NULL;
    }
    new->status = VPAGE_SWAPPED;
    new->uaddr = uaddr;
    new->backend.swap.swap_index = swap_idx;
    new->pid = pid;
    new->writable = writable;
    lock_acquire(&vm_lock);
    if ((old = hash_find(&vpage_info_map, &new->elem)) != NULL) {
        free(new);
        new = NULL;
        goto done;
    }
    hash_insert(&vpage_info_map, &new->elem);
done:
    lock_release(&vm_lock);
    return new;
}

void
vpage_info_release(struct vpage_info *vpi) {
    lock_acquire(&vm_lock);
    vpage_info_release_inner(vpi);
    lock_release(&vm_lock);
}

void vpage_info_find_and_release(void *upage, pid_t pid) {
    lock_acquire(&vm_lock);
    struct hash_iterator i;
    hash_first(&i, &vpage_info_map);
    while(hash_next(&i)) {
        struct vpage_info *vpi = hash_entry(hash_cur(&i), struct vpage_info, elem);
        if (vpi->uaddr == upage && vpi->pid == pid) {
            vpage_info_release_inner(vpi);
            goto done;
        }
    }
done:
    lock_release(&vm_lock);
}

void vpage_info_set_writable(void *upage, pid_t pid, bool writable, bool *inmem) {
    lock_acquire(&vm_lock);
    struct hash_iterator i;
    hash_first(&i, &vpage_info_map);
    *inmem = false;
    while(hash_next(&i)) {
        struct vpage_info *vpi = hash_entry(hash_cur(&i), struct vpage_info, elem);
        if (vpi->uaddr == upage && vpi->pid == pid) {
            vpi->writable = writable;
            if (vpi->status == VPAGE_INMEM) {
                void *paddr = pagedir_get_page(vpi->backend.inmem.pagedir, upage);
                ASSERT(paddr);
                pagedir_clear_page(vpi->backend.inmem.pagedir, upage);
                pagedir_set_page(vpi->backend.inmem.pagedir, upage, paddr, writable);
                *inmem = true;
            }
            goto done;
        }
    }
done:
    lock_release(&vm_lock);
}

void vpage_info_release_all(pid_t pid) {
    lock_acquire(&vm_lock);
    struct hash_iterator i;
do_again:
    hash_first(&i, &vpage_info_map);
    while(hash_next(&i)) {
        struct vpage_info *vpi = hash_entry(hash_cur(&i), struct vpage_info, elem);
        if (vpi->pid == pid) {
            vpage_info_release_inner(vpi);
            goto do_again;
        }
    }
done:
    lock_release(&vm_lock);
}

struct vpage_info *vpage_info_find(void *upage, pid_t pid) {
    struct vpage_info *rv;
    lock_acquire(&vm_lock);
    struct hash_iterator i;
    hash_first(&i, &vpage_info_map);
    while(hash_next(&i)) {
        struct vpage_info *vpi = hash_entry(hash_cur(&i), struct vpage_info, elem);
        if (vpi->uaddr == upage && vpi->pid == pid) {
            rv = vpi;
            goto done;
        }
    }
rv = NULL;
done:
    lock_release(&vm_lock);
    return rv;
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