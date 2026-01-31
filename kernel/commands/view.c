#include "programs.h"
#include "apps/image_viewer.h"
#include "kmalloc.h"
#include "init.h"
#include "lib.h"
#include <string.h>

int prog_view(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    char *path = NULL;
    if (argc >= 2) {
        path = init_resolve_path(argv[1]);
        if (!path) {
            const char *fail = "invalid path\n";
            size_t m = strlen(fail); if(m>out_cap)m=out_cap; memcpy(out,fail,m);
            return (int)m;
        }
    }
    image_viewer_start(path);
    if (path) kfree(path);
    
    const char *ok = "opening viewer...\n";
    size_t m = strlen(ok); if(m>out_cap)m=out_cap; memcpy(out,ok,m);
    return (int)m;
}
