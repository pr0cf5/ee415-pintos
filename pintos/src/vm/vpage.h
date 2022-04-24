#include <list.h>
#include <hash.h>
#include <stdio.h>
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "filesys/off_t.h"

enum user_fault_type {
    UFAULT_KILL,
    UFAULT_CONTINUE,
};

enum vpage_status {
    VPAGE_LAZY,
    VPAGE_INMEM,
    VPAGE_SWAPPED,
};

struct info_lazy {
    struct file *file;
    off_t offset;
    size_t length;
};

struct info_inmem {
    void *paddr;
    uint32_t *pagedir;
    int64_t last_use;
};

struct info_swap {
    int swap_index;
};

union vpage_info_backend {
    struct info_lazy lazy;
    struct info_inmem inmem;
    struct info_swap swap;
};

struct vpage_info {
    enum vpage_status status;
    void *uaddr;
    bool writable;
    pid_t pid;
    union vpage_info_backend backend;
    struct hash_elem elem;
};

struct vpage_info *vpage_info_lazy_allocate(void *uaddr, struct file *file, off_t offset, size_t length, pid_t pid, bool writable);
struct vpage_info *vpage_info_inmem_allocate(void *uaddr, void *paddr, pid_t pid, bool writable);
struct vpage_info *vpage_info_swapped_allocate(void *uaddr, uint32_t swap_idx, pid_t pid, bool writable);
void vpage_info_release(struct vpage_info *vpi);
void vpage_info_find_and_release(void *upage, pid_t pid);
void vpage_info_set_writable(void *upage, pid_t pid, bool writable, bool *inmem);
struct vpage_info *vpage_info_find(void *upage, pid_t pid);
enum user_fault_type vpage_handle_user_fault(void *uaddr);