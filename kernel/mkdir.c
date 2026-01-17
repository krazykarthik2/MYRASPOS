#include "programs.h"
#include "init.h"
#include "lib.h"
#include "kmalloc.h"
#include <stddef.h>

int prog_mkdir(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 2) {
        const char *u = "usage: mkdir <name>\n";
        size_t m = strlen(u); if (m > out_cap) m = out_cap; memcpy(out, u, m); return (int)m;
    }
    char *path = init_resolve_path(argv[1]);
    if (!path) { const char *m = "fail\n"; size_t mm = strlen(m); if (mm>out_cap) mm=out_cap; memcpy(out,m,mm); return (int)mm; }
    int r = init_ramfs_mkdir(path);
    kfree(path);
    const char *msg = (r==0)?"ok\n":"fail\n";
    size_t m = strlen(msg); if (m>out_cap) m=out_cap; memcpy(out,msg,m); return (int)m;
}
