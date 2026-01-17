#include "programs.h"
#include "init.h"
#include "lib.h"
#include <stddef.h>

int prog_rm(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 2) {
        const char *u = "usage: rm <name>\n";
        size_t m = strlen(u); if (m > out_cap) m = out_cap; memcpy(out, u, m); return (int)m;
    }
    int r = -1;
    if (argc >= 3 && (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "-rf") == 0)) {
        /* recursive remove: resolve target */
        char *target = init_resolve_path(argv[2]);
        if (!target) { r = -1; }
        else { r = init_ramfs_remove_recursive(target); kfree(target); }
    } else if (argc >= 2 && (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "-rf") == 0)) {
        /* missing target */
        r = -1;
    } else {
        char *target = init_resolve_path(argv[1]);
        if (!target) r = -1; else { r = init_ramfs_remove(target); kfree(target); }
    }
    const char *msg = (r==0)?"ok\n":"fail\n";
    size_t m = strlen(msg); if (m>out_cap) m=out_cap; memcpy(out,msg,m); return (int)m;
}
