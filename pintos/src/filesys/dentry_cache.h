#include "filesys/inode.h"
#include "filesys/path.h"
void dentry_cache_init(void);
bool dentry_cache_query(const struct canon_path *cpath, size_t outer_level, int *inumber);
bool dentry_cache_invalidate(int number);
bool dentry_cache_append(const struct canon_path *cpath, size_t outer_level, int inumber);