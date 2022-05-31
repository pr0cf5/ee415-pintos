#include "filesys/dentry_cache.h"
#include "filesys/directory.h"
#include "threads/synch.h"
#include <hash.h>
#include <string.h>
#include <stdio.h>
#include <debug.h>

//#define DENTRY_CACHE 1

static struct hash dentry_cache;

struct dentry_cache_entry {
    struct hash_elem elem;
    char *path;
    int inumber;
};

static char *strdup(const char *in) {
    char *out;
    size_t length;
    length = strlen(in) + 1;
    if ((out = malloc(length)) == NULL) {
        return NULL;
    }
    strlcpy(out, in, length);
    return out;
}

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
    hash_init(&dentry_cache, dentry_cache_entry_hash_func, dentry_cache_entry_less, NULL);
}

// we don't need synchronization at the dentry cache layer, because synchronization is already implemented in the dir layer
// dentry cache only stores absoulte paths
bool dentry_cache_query(const struct canon_path *cpath, size_t outer_level, int *inumber) {
    bool success;
    char *path;
    size_t path_length;
    struct dentry_cache_entry *found;
    struct dentry_cache_entry dce;
    struct hash_elem *e;
    success = false;
#ifdef DENTRY_CACHE
    if (!canon_path_serialize(cpath, outer_level, &path, &path_length)) {
        return false;        
    }
    dce.inumber = 0;
    dce.path = path;
    if ((e = hash_find(&dentry_cache, &dce.elem))) {
        found = hash_entry(e, struct dentry_cache_entry, elem);
        *inumber = found->inumber;
        success = true;
        goto done;
    }
    else {
        success = false;
        goto done;
    }

done:
    free(path);
#endif
    return success;
}

bool dentry_cache_invalidate(int inumber) {
    bool success;
    success = false;
    struct hash_iterator i;
    success = false;
#ifdef DENTRY_CACHE
do_again:
    hash_first(&i, &dentry_cache);
    while(hash_next(&i)) {
        struct dentry_cache_entry *dce = hash_entry(hash_cur(&i), struct dentry_cache_entry, elem);
        if (dce->inumber == inumber) {
            success = true;
            hash_delete(&dentry_cache, &dce->elem);
            free(dce);
            goto do_again;
        }
    }
done:
#endif
    return success;
}

bool dentry_cache_append(const struct canon_path *cpath, size_t outer_level, int inumber) {
     bool success;
    char *path;
    size_t path_length;
    struct dentry_cache_entry *new, *old;
    struct hash_elem *e;
    success = false;
#ifdef DENTRY_CACHE
    if (!canon_path_serialize(cpath, outer_level, &path, &path_length)) {
        return false;        
    }
    if ((new = malloc(sizeof(*new))) == NULL) {
        goto done;
    }
    new->path = strdup(path);
    new->inumber = inumber;
    if (e = (hash_insert(&dentry_cache, &new->elem))) {
        free(new);
        success = true;
        goto done;
    }
done:
    free(path);
#endif
    return success;
}