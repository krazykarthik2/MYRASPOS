#include "programs.h"
#include "init.h"
#include "lib.h"
#include "kmalloc.h"
#include <stddef.h>
/* length-aware substring search */
static int match_substr_n(const char *line, size_t line_len, const char *pat) {
    size_t pat_len = strlen(pat);
    if (pat_len == 0) return 1;
    if (pat_len > line_len) return 0;
    for (size_t i = 0; i + pat_len <= line_len; ++i) {
        size_t j = 0;
        for (; j < pat_len; ++j) if (line[i+j] != pat[j]) break;
        if (j == pat_len) return 1;
    }
    return 0;
}

int prog_grep(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    if (argc < 2) {
        const char *u = "usage: grep <pattern> [file]\n";
        size_t m = strlen(u); if (m > out_cap) m = out_cap; memcpy(out, u, m); return (int)m;
    }
    const char *pat = argv[1];
    size_t off = 0;
    if (argc >= 3) {
        /* read file into buffer and scan lines manually */
        char *buf = kmalloc(4096);
        if (!buf) return -1;
        int r = init_ramfs_read(argv[2], buf, 4096);
        if (r > 0) {
            size_t len = (size_t)r;
            size_t start = 0;
            for (size_t i = 0; i <= len; ++i) {
                if (i == len || buf[i] == '\n' || buf[i] == '\r') {
                    size_t L = i - start;
                    if (L > 0 && match_substr_n(&buf[start], L, pat)) {
                        if (off + L + 1 >= out_cap) break;
                        memcpy(out + off, &buf[start], L);
                        off += L;
                        out[off++] = '\n';
                    }
                    start = i + 1;
                }
            }
        }
        kfree(buf);
    } else {
        /* use provided stdin buffer */
        if (!in || in_len == 0) return 0;
        size_t start = 0;
        for (size_t i = 0; i <= in_len; ++i) {
            if (i == in_len || in[i] == '\n' || in[i] == '\r') {
                size_t L = i - start;
                if (L > 0 && match_substr_n(&in[start], L, pat)) {
                    if (off + L + 1 >= out_cap) break;
                    memcpy(out + off, &in[start], L);
                    off += L;
                    out[off++] = '\n';
                }
                start = i + 1;
            }
        }
    }
    return (int)off;
}
