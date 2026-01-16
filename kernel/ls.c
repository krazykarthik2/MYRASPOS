#include "programs.h"
#include "init.h"
#include "lib.h"
#include <stddef.h>

int prog_ls(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    const char *dir = "/";
    if (argc >= 2) {
        if (strcmp(argv[1], ".") == 0) dir = "/";
        else dir = argv[1];
    }
    int r = init_ramfs_list(dir, out, out_cap);
    if (r < 0) return (int)r;
    return r;
}
