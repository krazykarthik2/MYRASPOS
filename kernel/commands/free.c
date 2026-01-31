#include "programs.h"
#include "palloc.h"
#include "lib.h"
/* Wait, existing shell used `cmd_free` but the code I read truncated before `cmd_free` impl. 
   I need to implement `prog_free` querying memory stats.
   CHECK: palloc.h has palloc_get_free_pages()?
*/
#include "uart.h" 
#include <string.h>

/* Helper to formatting int to string */
static void fmt_int(char *buf, int v) {
    if (v == 0) { strcpy(buf, "0"); return; }
    char tmp[16]; int i=0;
    while(v > 0) { tmp[i++] = (v%10)+'0'; v/=10; }
    int j=0; while(i>0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

int prog_free(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)argc; (void)argv; (void)in; (void)in_len;
    /* We need palloc stats */
    size_t free_pages = palloc_get_free_pages();
    size_t free_mem = free_pages * 4096;
    
    char buf[128];
    strcpy(buf, "Memory: ");
    int used = 8;
    
    char num[32];
    fmt_int(num, (int)free_mem);
    
    if (used + strlen(num) + 7 < sizeof(buf)) {
        strcat(buf, num);
        strcat(buf, " bytes\n");
        used += strlen(num) + 7;
    }
    
    size_t m = used;
    if (m > out_cap) m = out_cap;
    memcpy(out, buf, m);
    return (int)m;
}
