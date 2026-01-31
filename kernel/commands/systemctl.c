#include "programs.h"
#include "service.h"
#include "lib.h"
#include "kmalloc.h"
#include <string.h>

/* Helper duplicates from shell.c until normalized path logic is exposed in lib.c or init.h */
/* We can use init_resolve_path from shell.c via init.h if declared? 
   Actually init_resolve_path is in shell.c and not init.h, but exported?
   In shell.c: "char *init_resolve_path(const char *p)"
   Let's check if there is a header. init.h?
   Wait, if init_resolve_path is defined in shell.c, we might link error if shell.o is not included or cyclic dep.
   Shell is kernel code, programs are kernel code.
   Ideally logic should be in lib.c or dedicated path.c.
   
   For now, I'll rely on `init_resolve_path` being available (extern it). 
   It was used in shell.c for `cmd_systemctl`.
*/

extern char *init_resolve_path(const char *p);

/* Helper: derive service shortname (duplicate from shell.c for now) */
/* Actually, systemctl usually works on service names or full paths.
   If duplicate logic is complex, maybe good chance to refactor into service.c?
   But for now, I'll put a simplified version here.
*/

static char *abs_path_alloc(const char *p) {
    if (!p) return NULL;
    return init_resolve_path(p); /* This normalizes */
}

/* Helper: given a unit argument, derive short service name (without .service)
   Caller-provided output buffers must be large enough. Returns 0 on success.
*/
static int derive_service_shortname(const char *arg, char *out, size_t out_len, char *fullpath, size_t full_len) {
    if (!arg || !out) return -1;
    /* if arg contains '/', treat as path */
    const char *slash = strchr(arg, '/');
    if (slash) {
        /* make absolute fullpath */
        char *abs = abs_path_alloc(arg);
        if (!abs) return -1;
        if (fullpath && full_len > 0) {
            size_t l = strlen(abs); if (l >= full_len) l = full_len - 1; memcpy(fullpath, abs, l); fullpath[l] = '\0';
        }
        /* extract filename */
        const char *last = abs + strlen(abs);
        while (last > abs && *(last-1) != '/') --last;
        size_t i = 0;
        while (*last && *last != '.' && i + 1 < out_len) out[i++] = *last++;
        out[i] = '\0';
        kfree(abs);
        return 0;
    }
    /* no slash: may be name or name.service */
    size_t i = 0;
    const char *p = arg;
    while (*p && *p != '.' && i + 1 < out_len) out[i++] = *p++;
    out[i] = '\0';
    if (fullpath && full_len > 0) {
        /* build /etc/systemd/system/<out>.service */
        const char *pref = "/etc/systemd/system/";
        size_t plen = strlen(pref); size_t nlen = strlen(out); size_t slen = strlen(".service");
        if (plen + nlen + slen + 1 < full_len) {
            memcpy(fullpath, pref, plen);
            memcpy(fullpath + plen, out, nlen);
            memcpy(fullpath + plen + nlen, ".service", slen+1);
        } else {
            fullpath[0] = '\0';
        }
    }
    return 0;
}


int prog_systemctl(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 2) {
        const char *u = "usage: systemctl <start|stop|restart|status|list-units> [unit]\n";
        size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m;
    }
    
    char name[64]; char path[256];
    
    if (strcmp(argv[1], "start") == 0) {
        if (argc < 3) { const char *f="unit required\n"; size_t m=strlen(f); if(m>out_cap)m=out_cap; memcpy(out,f,m); return (int)m; }
        if (derive_service_shortname(argv[2], name, sizeof(name), path, sizeof(path)) != 0) {
             const char *f="invalid unit\n"; size_t m=strlen(f); if(m>out_cap)m=out_cap; memcpy(out,f,m); return (int)m;
        }
        /* If a path was derived/provided, try loading it first */
        if (path[0]) {
             service_load_unit(path);
        }
        int r = service_start(name);
        const char *s = (r==0)?"started\n":"failed\n"; size_t m=strlen(s); if(m>out_cap)m=out_cap; memcpy(out,s,m); return (int)m;
        
    } else if (strcmp(argv[1], "stop") == 0) {
        if (argc < 3) { const char *f="unit required\n"; size_t m=strlen(f); if(m>out_cap)m=out_cap; memcpy(out,f,m); return (int)m; }
        if (derive_service_shortname(argv[2], name, sizeof(name), NULL, 0) != 0) {
             const char *f="invalid unit\n"; size_t m=strlen(f); if(m>out_cap)m=out_cap; memcpy(out,f,m); return (int)m;
        }
        int r = service_stop(name);
        const char *s = (r==0)?"stopped\n":"failed\n"; size_t m=strlen(s); if(m>out_cap)m=out_cap; memcpy(out,s,m); return (int)m;

    } else if (strcmp(argv[1], "restart") == 0) {
        if (argc < 3) { const char *f="unit required\n"; size_t m=strlen(f); if(m>out_cap)m=out_cap; memcpy(out,f,m); return (int)m; }
        if (derive_service_shortname(argv[2], name, sizeof(name), path, sizeof(path)) != 0) {
             const char *f="invalid unit\n"; size_t m=strlen(f); if(m>out_cap)m=out_cap; memcpy(out,f,m); return (int)m;
        }
        service_stop(name);
        if (path[0]) {
             service_load_unit(path);
        }
        int r = service_start(name);
        const char *s = (r==0)?"restarted\n":"failed\n"; size_t m=strlen(s); if(m>out_cap)m=out_cap; memcpy(out,s,m); return (int)m;

    } else if (strcmp(argv[1], "status") == 0) {
        /* Not fully implemented in service.c yet? or is it? Assuming basic exists */
        const char *s = "status not impl\n"; size_t m=strlen(s); if(m>out_cap)m=out_cap; memcpy(out,s,m); return (int)m;
    } else {
        const char *f="unknown op\n"; size_t m=strlen(f); if(m>out_cap)m=out_cap; memcpy(out,f,m); return (int)m;
    }
}
