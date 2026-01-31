#include "programs.h"
#include "lib.h"
#include <stddef.h>


/* Duplicate helper from shell.c until we move it to lib.c or similar */
/* process common backslash escapes into dst, return bytes written (no NUL) */
static size_t process_escapes(const char *src, char *dst, size_t dst_cap) {
    size_t di = 0;
    for (const char *p = src; *p && di < dst_cap; ++p) {
        if (*p == '\\' && *(p+1)) {
            ++p;
            char c = *p;
            char outc = c;
            if (c == 'n') outc = '\n';
            else if (c == 't') outc = '\t';
            else if (c == 'r') outc = '\r';
            else if (c == '\\') outc = '\\';
            else if (c == '0') outc = '\0';
            else outc = c;
            if (di < dst_cap) dst[di++] = outc;
        } else {
            dst[di++] = *p;
        }
    }
    return di;
}

int prog_echo(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    size_t off = 0;
    int start = 1;
    int interpret = 0;
    if (argc >= 2 && strcmp(argv[1], "-e") == 0) { interpret = 1; start = 2; }
    for (int i = start; i < argc; ++i) {
        if (interpret) {
            /* process escapes into a temporary small buffer then copy */
            char tmp[256]; size_t w = process_escapes(argv[i], tmp, sizeof(tmp));
            if (off + w >= out_cap) { size_t can = out_cap - off; memcpy(out+off, tmp, can); off += can; break; }
            memcpy(out+off, tmp, w); off += w;
        } else {
            size_t l = strlen(argv[i]);
            if (off + l >= out_cap) break;
            memcpy(out+off, argv[i], l);
            off += l;
        }
        if (i + 1 < argc) {
            if (off + 1 >= out_cap) break;
            out[off++] = ' ';
        }
    }
    if (off < out_cap) out[off++] = '\n';
    return (int)off;
}


