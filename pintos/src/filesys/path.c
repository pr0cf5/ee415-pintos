#include "filesys/path.h"
#include "threads/malloc.h"
#include <string.h>
#include <list.h>

struct path_token {
    char *value;
    struct list_elem elem;
};

struct canon_path {
    bool absolute;
    size_t tokens_cnt;
    char **tokens;
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

size_t canon_path_get_tokens_cnt(struct canon_path *cpath) {
    return cpath->tokens_cnt;
}

char *canon_path_get_token(struct canon_path *cpath, size_t index) {
    return cpath->tokens[index];
}

char *canon_path_get_leaf(struct canon_path *cpath) {
    return cpath->tokens[cpath->tokens_cnt-1];
}

bool canon_path_is_absolute(struct canon_path *cpath) {
    return cpath->absolute;
}

void canon_path_release(struct canon_path *cpath) {
    for (int i = 0; i < cpath->tokens_cnt; i++) {
        free(cpath->tokens[i]);
    }
    free(cpath->tokens);
    free(cpath);
}

bool path_canonicalize(const char *path, struct canon_path **out) {
    char *path_cpy, *next_ptr, *probe;
    struct list tokens;
    size_t tokens_cnt, i;
    bool absolute, success;
    char **tokens_buf;
    struct canon_path *rv;
    if ((path_cpy = strdup(path)) == NULL) {
        success = false;
        goto done;
    }
    tokens_cnt = 0;
    list_init(&tokens);
    if (path == NULL || out == NULL) {
        return false;
    }
    if (path_cpy[0] == '/') {
        // absolute path
        absolute  = true;
        path = path_cpy + 1;
        
    }
    else {
        // relative path
        absolute = false;
        path = path_cpy;
    }
    probe = strtok_r(path, "/", &next_ptr);
    while (probe != NULL) {
        if (!strcmp(probe, "..")) {
            if (!list_empty(&tokens)) {
                list_pop_back(&tokens);
                tokens_cnt--;
            }
            else {
                if (absolute) {
                    // parent of root is root. do nothing
                }
                else {
                    success = false;
                    goto done;
                }
            }
        }
        else if (!strcmp(probe, ".")) {
            // do nothing
        }
        else {
            // handle
            struct path_token *newtok = malloc(sizeof(struct path_token));
            if ((newtok->value = strdup(probe)) == NULL) {
                success = false;
                goto done;
            }
            list_push_back(&tokens, &newtok->elem);
            tokens_cnt++;
        }
        probe = strtok_r(NULL, "/", &next_ptr);
    }
    // copy tokens in list to output
    tokens_buf = calloc(sizeof(char *), tokens_cnt);
    i = 0;
    while(!list_empty(&tokens)) {
        struct list_elem *e = list_pop_front(&tokens);
        struct path_token *pt = list_entry(e, struct path_token, elem);
        tokens_buf[i++] = pt->value;
        free(pt);
    }
    rv = malloc(sizeof(struct canon_path));
    if (!rv) {
        success = false;
        goto done;
    }
    rv->absolute = absolute;
    rv->tokens = tokens_buf;
    rv->tokens_cnt = tokens_cnt;
    *out = rv;
    success = true;
done:
    free(path_cpy);
    return success;
}