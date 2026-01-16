#include "programs.h"
#include "init.h"
#include "lib.h"
#include "kmalloc.h"
#include <stddef.h>

int prog_cp(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 3) {
        const char *u = "usage: cp <src> <dst>\n";
        size_t m = strlen(u); if (m > out_cap) m = out_cap; memcpy(out, u, m); return (int)m;
    }
    char *buf = kmalloc(4096);
    if (!buf) return -1;
    int r = init_ramfs_read(argv[1], buf, 4096);
    if (r < 0) { kfree(buf); const char *f = "fail\n"; size_t m = strlen(f); if (m>out_cap) m=out_cap; memcpy(out,f,m); return (int)m; }
    init_ramfs_remove(argv[2]);
    init_ramfs_create(argv[2]);
    int w = init_ramfs_write(argv[2], buf, (size_t)r, 0);
    kfree(buf);
    const char *msg = (w>=0)?"ok\n":"fail\n"; size_t m = strlen(msg); if (m>out_cap) m=out_cap; memcpy(out,msg,m); return (int)m;
}
