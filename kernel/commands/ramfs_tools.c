#include "programs.h"
#include "init.h"
#include "lib.h"
#include "kmalloc.h"
#include <string.h>

extern char *init_resolve_path(const char *p);

static char *abs_path_alloc(const char *p) {
    if (!p) return NULL;
    return init_resolve_path(p);
}


int prog_ramfs_export(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 2) { const char *u = "usage: ramfs-export <path>\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m; }
    char *ap = abs_path_alloc(argv[1]);
    if (!ap) { const char *s = "failed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m; }
    int r = init_ramfs_export(ap);
    kfree(ap);
    const char *s = (r==0) ? "exported\n" : "failed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
}

int prog_ramfs_import(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 2) { const char *u = "usage: ramfs-import <path>\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m; }
    char *ap = abs_path_alloc(argv[1]);
    if (!ap) { const char *s = "failed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m; }
    int r = init_ramfs_import(ap);
    kfree(ap);
    const char *s = (r==0) ? "imported\n" : "failed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
}
