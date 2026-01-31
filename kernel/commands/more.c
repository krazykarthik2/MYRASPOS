#include "programs.h"
#include "init.h"
#include "lib.h"
#include "kmalloc.h"
#include <stddef.h>

int prog_more(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    int lines = -1; /* -1 => print all */
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

    const char *data = NULL; size_t data_len = 0; char *buf = NULL;
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

    if (lines < 0) {
        size_t tocopy = data_len;
        if (tocopy > out_cap) tocopy = out_cap;
        memcpy(out, data, tocopy);
        if (buf) kfree(buf);
        return (int)tocopy;
    }

    /* copy up to 'lines' newline-terminated lines */
    size_t pos = 0; size_t off = 0; int rem = lines;
    while (pos < data_len && rem > 0) {
        char c = data[pos++];
        if (off + 1 >= out_cap) break;
        out[off++] = c;
        if (c == '\n') rem--;
    }
    if (buf) kfree(buf);
    return (int)off;
}
