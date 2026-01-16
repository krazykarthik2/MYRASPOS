#include "programs.h"
#include "lib.h"
#include <stddef.h>

int prog_echo(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    size_t off = 0;
    for (int i = 1; i < argc; ++i) {
        size_t l = strlen(argv[i]);
        if (off + l >= out_cap) break;
        memcpy(out + off, argv[i], l);
        off += l;
        if (i + 1 < argc) {
            if (off + 1 >= out_cap) break;
            out[off++] = ' ';
        }
    }
    if (off < out_cap) out[off++] = '\n';
    return (int)off;
}
