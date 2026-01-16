#include "programs.h"
#include "init.h"
#include "lib.h"
#include "kmalloc.h"
#include <stddef.h>

int prog_write(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 3) {
        const char *u = "usage: write <name> <text>\n";
        size_t m = strlen(u); if (m > out_cap) m = out_cap; memcpy(out, u, m); return (int)m;
    }
    const char *name = argv[1];
    /* join remaining args into text */
    size_t total = 0;
    for (int i = 2; i < argc; ++i) total += strlen(argv[i]) + 1;
    char *text = kmalloc(total + 1);
    if (!text) return -1;
    size_t off = 0; for (int i = 2; i < argc; ++i) { size_t l = strlen(argv[i]); memcpy(text+off, argv[i], l); off += l; if (i+1<argc) text[off++]=' '; }
    text[off] = 0;
    init_ramfs_remove(name);
    init_ramfs_create(name);
    int w = init_ramfs_write(name, text, off, 0);
    kfree(text);
    const char *msg = (w >= 0) ? "wrote\n" : "fail\n"; size_t m = strlen(msg); if (m > out_cap) m = out_cap; memcpy(out, msg, m); return (int)m;
}
