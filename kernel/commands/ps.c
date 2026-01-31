#include "programs.h"
#include "sched.h"
#include "lib.h"
#include <string.h>

/* Helper to formatting int to string */
static void fmt_int(char *buf, int v) {
    if (v == 0) { strcpy(buf, "0"); return; }
    char tmp[16]; int i=0;
    while(v > 0) { tmp[i++] = (v%10)+'0'; v/=10; }
    int j=0; while(i>0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

int prog_ps(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)argc; (void)argv; (void)in; (void)in_len;
    /* need task stats from sched.h */
    /* sched.h has task_stats(int *ids, int *runs, int *ticks, int *runnable, char *names, int max, int *total) */
    
    int ids[32];
    int runs[32];
    int ticks[32]; /* start tick */
    int runnable[32];
    char names[32*16];
    int total_runs = 0;
    
    int count = task_stats(ids, runs, ticks, runnable, names, 32, &total_runs);
    
    size_t off = 0;
    const char *hdr = "PID  NAME             RUNS\n"; /* simplified */
    size_t hl = strlen(hdr); 
    if (off + hl < out_cap) { memcpy(out+off, hdr, hl); off += hl; }
    
    for (int i = 0; i < count; ++i) {
        char line[64];
        char num[16];
        
        /* PID */
        fmt_int(num, ids[i]);
        int pad = 5 - (int)strlen(num);
        if (pad < 1) pad = 1;
        
        size_t l_idx = 0; // index in line buffer
        memcpy(line + l_idx, num, strlen(num)); l_idx += strlen(num);
        for(int k=0;k<pad;k++) line[l_idx++] = ' ';
        
        /* NAME */
        const char *nm = names + i*16;
        size_t nlen = strlen(nm);
        memcpy(line + l_idx, nm, nlen); l_idx += nlen;
        pad = 17 - (int)nlen; 
        if (pad < 1) pad = 1;
        for(int k=0;k<pad;k++) line[l_idx++] = ' ';
        
        /* RUNS */
        fmt_int(num, runs[i]);
        memcpy(line + l_idx, num, strlen(num)); l_idx += strlen(num);
        line[l_idx++] = '\n';
        line[l_idx] = '\0';
        
        size_t line_len = strlen(line);
        if (off + line_len >= out_cap) break;
        memcpy(out + off, line, line_len);
        off += line_len;
    }
    
    if (off < out_cap) out[off] = '\0';
    return (int)off;
}
