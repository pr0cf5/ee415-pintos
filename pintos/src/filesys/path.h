#include <stdbool.h>
#include <stddef.h>

struct canon_path;
size_t canon_path_get_tokens_cnt(struct canon_path *cpath);
char *canon_path_get_token(struct canon_path *cpath, size_t index);
char *canon_path_get_leaf(struct canon_path *cpath);
bool canon_path_is_absolute(struct canon_path *cpath);
void canon_path_release(struct canon_path *cpath);
bool path_canonicalize(const char *path, struct canon_path **out);