#include "programs.h"
#include "init.h"
#include "lib.h"
#include "kmalloc.h"
#include "glob.h"
#include <stddef.h>

static int tree_walk(const char *dir, int depth, char *out, size_t out_cap, size_t *off) {
    if (*off >= out_cap) return 0;
    char listbuf[1024];
    int r = init_ramfs_list(dir, listbuf, sizeof(listbuf));
    if (r < 0) return r;
    size_t p = 0;
    while (p < (size_t)r) {
        /* find next newline-separated entry length */
        size_t l = 0;
        while (p + l < (size_t)r && listbuf[p + l] != '\n') ++l;
        if (l == 0) break;
        const char *name = &listbuf[p];
        /* print indent */
        for (int i = 0; i < depth; ++i) {
            if (*off + 2 >= out_cap) return 0;
            out[(*off)++] = ' ';
            out[(*off)++] = ' ';
        }
        /* build full path: full = dir + name; ensure slash handling */
        char full[256];
        size_t fl = 0;
        if (strcmp(dir, "/") == 0) {
            full[0] = '/'; full[1] = '\0'; fl = 1;
        } else {
            strncpy(full, dir, sizeof(full)-1);
            full[sizeof(full)-1] = '\0';
            fl = strlen(full);
        }
        /* append name (name is not NUL-terminated in listbuf) */
        if (fl + l + 1 < sizeof(full)) {
            memcpy(full + fl, name, l);
            full[fl + l] = '\0';
        }
        /* write name */
        if (*off + l + 1 >= out_cap) return 0;
        memcpy(out + *off, name, l);
        *off += l;
        out[(*off)++] = '\n';

        /* if directory (ends with /), recurse */
        if (l > 0 && name[l-1] == '/') {
            /* ensure full path ends with slash */
            if (full[strlen(full)-1] != '/') {
                size_t fll = strlen(full);
                if (fll + 1 < sizeof(full)) { full[fll] = '/'; full[fll+1] = '\0'; }
            }
            tree_walk(full, depth+1, out, out_cap, off);
        }

        p += l + 1;
    }
    return 0;
}

int prog_tree(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)argc; (void)argv; (void)in; (void)in_len;
    const char *arg = NULL;
    char *resolved = NULL;
    if (argc >= 2) arg = argv[1];
    size_t off = 0;
    if (!arg) {
        char *cwd = init_resolve_path(".");
        if (!cwd) return -1;
        tree_walk(cwd, 0, out, out_cap, &off);
        kfree(cwd);
        return (int)off;
    }
    /* detect glob */
    int has_glob = 0;
    for (const char *p = arg; *p; ++p) { if (*p=='*' || *p=='?' || *p=='[') { has_glob = 1; break; } }
    if (has_glob) {
        /* split dir and pattern */
        const char *last = arg + strlen(arg);
        while (last > arg && *(last-1) != '/') --last;
        char dir[128]; char pat[128];
        if (last == arg) { strcpy(dir, "/"); strncpy(pat, arg, sizeof(pat)-1); pat[sizeof(pat)-1] = '\0'; }
        else {
            size_t dlen = (size_t)(last - arg);
            if (dlen >= sizeof(dir)) dlen = sizeof(dir)-1;
            memcpy(dir, arg, dlen); dir[dlen] = '\0';
            strncpy(pat, last, sizeof(pat)-1); pat[sizeof(pat)-1] = '\0';
        }
        char listbuf[1024];
        resolved = init_resolve_path(dir);
        if (!resolved) return -1;
        int r = init_ramfs_list(resolved, listbuf, sizeof(listbuf));
        kfree(resolved); resolved = NULL;
        if (r < 0) return (int)r;
        size_t p = 0;
        /* ramfs_list uses '\n' separators */
        while (p < (size_t)r) {
            size_t l = 0; while (p + l < (size_t)r && listbuf[p + l] != '\n') ++l;
            if (l == 0) break;
            const char *name = &listbuf[p];
            if (glob_match(pat, name)) {
                /* print matched name (and recurse if directory) */
                /* indent 0 */
                if (off + l + 1 >= out_cap) break;
                memcpy(out+off, name, l); off += l; out[off++] = '\n';
                if (name[l-1] == '/') {
                    /* build full path */
                    char full[256]; size_t fl = 0;
                    if (strcmp(dir, "/") == 0) { full[0] = '/'; full[1] = '\0'; fl = 1; }
                    else { strncpy(full, dir, sizeof(full)-1); full[sizeof(full)-1] = '\0'; fl = strlen(full); }
                    if (fl + l + 1 < sizeof(full)) memcpy(full + fl, name, l + 1);
                    if (full[strlen(full)-1] != '/') { size_t fll = strlen(full); if (fll + 1 < sizeof(full)) { full[fll] = '/'; full[fll+1] = '\0'; } }
                    tree_walk(full, 1, out, out_cap, &off);
                }
            }
            p += l + 1;
        }
        if (off < out_cap) out[off] = '\0';
        return (int)off;
    }
    /* no glob: if dir-like, call tree_walk, else if exact file -> print name */
    {
        char listbuf[256];
        resolved = init_resolve_path(arg);
        if (!resolved) return -1;
        int r = init_ramfs_list(resolved, listbuf, sizeof(listbuf));
        if (r > 0) {
            tree_walk(resolved, 0, out, out_cap, &off);
            kfree(resolved);
            return (int)off;
        }
        kfree(resolved); resolved = NULL;
    }
    /* check file exists */
    char tmp[4];
    resolved = init_resolve_path(arg);
    if (!resolved) return -1;
    int ex = init_ramfs_read(resolved, tmp, sizeof(tmp));
    kfree(resolved); resolved = NULL;
    if (ex >= 0) {
        const char *last = arg + strlen(arg);
        while (last > arg && *(last-1) != '/') --last;
        size_t l = strlen(last);
        if (l + 1 > out_cap) l = out_cap - 1;
        memcpy(out, last, l); out[l] = '\0'; return (int)l;
    }
    return 0;
}
