#include "programs.h"
#include "init.h"
#include "lib.h"
#include "kmalloc.h"
#include <stddef.h>

int prog_tail(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    int lines = 10;
    const char *file = NULL;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'n' && argv[i][2] == '\0' && i+1 < argc) {
                lines = atoi(argv[++i]);
            } else if (argv[i][1] != '\0') {
                lines = atoi(&argv[i][1]);
            }
        } else {
            file = argv[i];
        }
    }

    const char *data = NULL;
    size_t data_len = 0;
    char *buf = NULL;
    if (file) {
        buf = kmalloc(4096);
        if (!buf) return -1;
        int r = init_ramfs_read(file, buf, 4096);
        if (r <= 0) { kfree(buf); return 0; }
        data = buf; data_len = (size_t)r;
    } else {
        if (!in || in_len == 0) return 0;
        data = in; data_len = in_len;
    }

    /* find start of last N lines */
    if (data_len == 0) { if (buf) kfree(buf); return 0; }
    size_t i = data_len;
    int found = 0;
    while (i > 0 && found < lines) {
        --i;
        if (data[i] == '\n') found++;
    }
    size_t start = (found >= lines && i < data_len) ? i+1 : 0;
    size_t tocopy = data_len - start;
    if (tocopy > out_cap) tocopy = out_cap;
    memcpy(out, data + start, tocopy);
    if (buf) kfree(buf);
    return (int)tocopy;
}
