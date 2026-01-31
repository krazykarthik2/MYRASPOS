#include "programs.h"
#include "sched.h"
#include "lib.h"

int prog_wait(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len; (void)out; (void)out_cap;
    if (argc < 2) return 0;
    int pid = 0; for (char *p = argv[1]; *p; ++p) { if (*p >= '0' && *p <= '9') pid = pid*10 + (*p - '0'); }
    while (task_exists(pid)) yield();
    return 0;
}
