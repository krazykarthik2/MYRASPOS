#include "programs.h"
#include "sched.h"
#include "lib.h"
#include <string.h>

int prog_kill(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 2) {
        const char *u = "usage: kill <pid>\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m;
    }
    int pid = 0; for (char *p = argv[1]; *p; ++p) { if (*p >= '0' && *p <= '9') pid = pid*10 + (*p - '0'); }
    int r = task_kill(pid);
    if (r == 0) {
        const char *s = "killed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
    } else {
        const char *s = "no such pid\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
    }
}
