#include "programs.h"
#include "init.h"
#include "lib.h"

int prog_clear(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)argc; (void)argv; (void)in; (void)in_len; (void)out; (void)out_cap;
    /* ANSI clear screen + move cursor home */
    init_puts("\x1b[2J\x1b[H");
    return 0;
}
