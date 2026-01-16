#include "programs.h"
#include "init.h"
#include "lib.h"
#include "kmalloc.h"
#include <stddef.h>

static int tree_walk(const char *dir, int depth, char *out, size_t out_cap, size_t *off) {
    if (*off >= out_cap) return 0;
    char listbuf[1024];
    int r = init_ramfs_list(dir, listbuf, sizeof(listbuf));
    if (r < 0) return r;
    size_t p = 0;
    while (p < (size_t)r) {
        const char *name = &listbuf[p];
        size_t l = strlen(name);
        if (l == 0) break;
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
        /* append name if space permits */
        size_t namelen = strlen(name);
        if (fl + namelen + 1 < sizeof(full)) memcpy(full + fl, name, namelen + 1);
        /* write name */
        /* namelen already computed above */
        if (*off + namelen + 1 >= out_cap) return 0;
        memcpy(out + *off, name, namelen);
        *off += namelen;
        out[(*off)++] = '\n';

        /* if directory (ends with /), recurse */
        if (name[namelen-1] == '/') {
            /* ensure full path ends with slash */
            if (full[strlen(full)-1] != '/') {
                size_t fl = strlen(full);
                if (fl + 1 < sizeof(full)) { full[fl] = '/'; full[fl+1] = '\0'; }
            }
            tree_walk(full, depth+1, out, out_cap, off);
        }

        p += l + 1;
    }
    return 0;
}

int prog_tree(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)argc; (void)argv; (void)in; (void)in_len;
    const char *dir = "/";
    if (argc >= 2) dir = argv[1];
    size_t off = 0;
    tree_walk(dir, 0, out, out_cap, &off);
    return (int)off;
}
