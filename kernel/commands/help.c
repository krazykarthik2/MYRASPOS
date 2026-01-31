#include "programs.h"
#include "programs.h"
#include <stddef.h>
#include "lib.h"

int prog_help(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)argc; (void)argv; (void)in; (void)in_len;
    size_t off = 0;
    size_t count;
    const char **list = program_list(&count);
    for (size_t i = 0; i < count; ++i) {
        size_t l = strlen(list[i]);
        if (off + l + 1 >= out_cap) break;
        memcpy(out + off, list[i], l); off += l;
        out[off++] = '\n';
    }
    return (int)off;
}
