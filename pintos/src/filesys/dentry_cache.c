#include "filesys/dentry_cache.h"
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "threads/synch.h"
#include <hash.h>
#include <string.h>

static struct hash dentry_cache;
static struct lock dentry_cache_lock;

struct dentry_cache_entry {
    struct hash_elem elem;
    char *path;
    int inumber;
};

static unsigned int
dentry_cache_entry_hash_func(const struct hash_elem *e) {
    return hash_string(hash_entry(e, struct dentry_cache_entry, elem)->path);
} 

static bool
dentry_cache_entry_less(const struct hash_elem *e1_, const struct hash_elem *e2_, void *aux) {
    struct dentry_cache_entry *e1, *e2;
    e1 = hash_entry(e1_, struct dentry_cache_entry, elem);
    e2 = hash_entry(e2_, struct dentry_cache_entry, elem);
    if (!strcmp(e1->path, e2->path)) {
        return false;
    }
    else {
        return true;
    }
}

void dentry_cache_init() {
    lock_init(&dentry_cache_lock);
    hash_init(&dentry_cache, dentry_cache_entry_hash_func, dentry_cache_entry_less, NULL);
}

bool
dentry_cache_query(const char *path, int *inumber) {
    bool success;
    lock_acquire(&dentry_cache_lock);

done:
    lock_release(&dentry_cache_lock);
    return success;
}

bool
dentry_cache_invalidate(const char *path) {
    bool success;
    lock_acquire(&dentry_cache_lock);
    
done:
    lock_release(&dentry_cache_lock);
    return success;
}

