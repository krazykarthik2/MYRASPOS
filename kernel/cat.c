#include "programs.h"
#include "init.h"
#include "lib.h"
#include <stddef.h>

int prog_cat(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 2) {
        const char *u = "usage: cat <name>\n";
        size_t m = strlen(u); if (m > out_cap) m = out_cap; memcpy(out, u, m); return (int)m;
    }
    int r = init_ramfs_read(argv[1], out, out_cap);
    if (r < 0) { const char *f = "fail\n"; size_t m = strlen(f); if (m > out_cap) m = out_cap; memcpy(out, f, m); return (int)m; }
    return r;
}
