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
    ASSERT(cpath->tokens > 0);
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

bool canon_path_serialize(const struct canon_path *cpath, size_t outer_level, char **out_, size_t *out_length_) {
    size_t out_length, offset, tok_length, tokens_cnt;
    char *out;
    out_length = 1;
    if (cpath->tokens_cnt <= outer_level) {
        return false;
    }
    tokens_cnt = cpath->tokens_cnt - outer_level;
    for (int i = 0; i < tokens_cnt; i++) {
        out_length += strlen(cpath->tokens[i]);
        out_length += 1; // for /
    }
    if ((out = malloc(out_length+1)) == NULL) {
        return false;
    }
    offset = 0;
    *(out + offset) = '/';
    offset += 1;
    for (int i = 0; i < tokens_cnt; i++) {
        tok_length = strlen(cpath->tokens[i]);
        memcpy(out + offset, cpath->tokens[i], tok_length);
        offset += tok_length;
        if (i < tokens_cnt - 1) {
            *(out + offset) = '/';
            offset += 1;
        }
    }
    *(out + offset) = '\0';
    *out_ = out;
    *out_length_ = out_length;
    return true;
}

bool path_canonicalize(const char *path, struct canon_path **out) {
    char *path_cpy, *next_ptr, *probe;
    struct list tokens;
    size_t tokens_cnt, i;
    bool absolute, success;
    char **tokens_buf;
    struct canon_path *rv;
    if (strlen(path) == 0) {
        return false;
    }
    if ((path_cpy = strdup(path)) == NULL) {
        success = false;
        return success;
    }
    tokens_cnt = 0;
    list_init(&tokens);
    if (path == NULL || out == NULL) {
        success = false;
        goto done;
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
                    // must go out of cwd
                    struct path_token *newtok;
                    if (!(newtok = malloc(sizeof(struct path_token)))) {
                        success = false;
                        goto done;
                    }
                    if (!(newtok->value = strdup(".."))) {
                        success = false;
                        goto done;
                    }
                    list_push_back(&tokens, &newtok->elem);
                    tokens_cnt++;
                }
            }
        }
        else if (!strcmp(probe, ".")) {
            // do nothing
        }
        else {
            // handle
            struct path_token *newtok;
            if (!(newtok = malloc(sizeof(struct path_token)))) {
                success = false;
                goto done;
            }
            if (!(newtok->value = strdup(probe))) {
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