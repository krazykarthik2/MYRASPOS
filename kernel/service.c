#include "service.h"
#include "ramfs.h"
#include "kmalloc.h"
#include "lib.h"
#include "sched.h"
#include "programs.h"
#include "uart.h"
#include <string.h>
#include <stddef.h>

#define SRV_NAME_MAX 64

struct service_entry {
    char name[SRV_NAME_MAX]; /* short name without path */
    char *unit_path; /* full path in ramfs */
    char *exec; /* command line for ExecStart */
    char *redir_target; /* optional redirection target for stdout */
    int redir_append;
    int enabled;
    int pid; /* task id of running service, 0 if not running */
    struct service_entry *next;
};

static struct service_entry *services = NULL;

static struct service_entry *find_service(const char *name) {
    for (struct service_entry *s = services; s; s = s->next) {
        if (strcmp(s->name, name) == 0) return s;
    }
    return NULL;
}

/* free_service_entry removed (unused) */

/* service task wrapper: calls program function once and exits */
struct svc_task_arg { char **argv; int argc; struct service_entry *svc; void *mem; };

static void service_task_fn(void *arg) {
    struct svc_task_arg *a = (struct svc_task_arg *)arg;
    /* debug: announce service task start */
    if (!a->svc || !a->svc->redir_target) {
        if (a && a->svc) {
            uart_puts("[svc] task start: "); uart_puts(a->svc->name); uart_puts("\n");
        } else {
            uart_puts("[svc] task start: (no svc)\n");
        }
    }
    /* find program and run it */
    if (a->argc > 0) {
        prog_fn_t pfn = NULL;
        if (program_lookup(a->argv[0], &pfn) == 0 && pfn) {
            if (!a->svc || !a->svc->redir_target) {
                uart_puts("[svc] program found: "); uart_puts(a->argv[0]); uart_puts("\n");
            }
            char out[256];
            int wrote = pfn(a->argc, a->argv, NULL, 0, out, sizeof(out));
            if (wrote > 0) {
                /* ensure NUL termination then print to console or file */
                size_t w = (wrote < (int)sizeof(out)-1) ? (size_t)wrote : (size_t)sizeof(out)-1;
                out[w] = '\0';
                if (a->svc && a->svc->redir_target) {
                    const char *tgt = a->svc->redir_target;
                    char *full = NULL;
                    if (tgt[0] == '/') {
                        full = kmalloc(strlen(tgt) + 1);
                        if (full) memcpy(full, tgt, strlen(tgt) + 1);
                    } else {
                        full = kmalloc(strlen(tgt) + 2);
                        if (full) { full[0] = '/'; memcpy(full+1, tgt, strlen(tgt)+1); }
                    }
                    if (full) {
                        /* ensure parent directory exists */
                        size_t fl = strlen(full);
                        if (fl > 1) {
                            size_t i = fl - 1; while (i > 0 && full[i] != '/') --i;
                            if (i > 0) {
                                char parent[256]; size_t plen = (i < sizeof(parent)-2) ? i : (sizeof(parent)-2);
                                memcpy(parent, full, plen); parent[plen] = '\0';
                                ramfs_mkdir(parent);
                            }
                        }
                        /* overwrite or create file */
                        ramfs_remove(full);
                        ramfs_create(full);
                        ramfs_write(full, out, w, 0);
                        if (!a->svc || !a->svc->redir_target) uart_puts("[svc] write complete\n");
                        kfree(full);
                    }
                } else if (!a->svc || !a->svc->redir_target) {
                    uart_puts(out);
                }
            } else if (!a->svc || !a->svc->redir_target) {
                uart_puts("[svc] program wrote nothing\n");
            }
        } else {
            if (!a->svc || !a->svc->redir_target) {
                uart_puts("[svc] program lookup failed for: "); uart_puts(a->argv[0]); uart_puts("\n");
            }
            /* fallback for simple builtins like 'echo' when program isn't registered */
            if (a->argc > 0 && strcmp(a->argv[0], "echo") == 0) {
                if (!a->svc || !a->svc->redir_target) uart_puts("[svc] fallback echo\n");
                char out[256]; size_t off = 0;
                for (int ai = 1; ai < a->argc; ++ai) {
                    size_t l = strlen(a->argv[ai]);
                    if (off + l >= sizeof(out)) l = sizeof(out) - off - 1;
                    if (l > 0) { memcpy(out + off, a->argv[ai], l); off += l; }
                    if (ai + 1 < a->argc) { if (off + 1 < sizeof(out)) out[off++] = ' '; }
                }
                if (off < sizeof(out)) out[off] = '\0'; else out[sizeof(out)-1] = '\0';
                if (a->svc && a->svc->redir_target) {
                    const char *tgt = a->svc->redir_target;
                    char *full = NULL;
                    if (tgt[0] == '/') {
                        full = kmalloc(strlen(tgt) + 1);
                        if (full) memcpy(full, tgt, strlen(tgt) + 1);
                    } else {
                        full = kmalloc(strlen(tgt) + 2);
                        if (full) { full[0] = '/'; memcpy(full+1, tgt, strlen(tgt)+1); }
                    }
                    if (full) {
                        size_t fl = strlen(full);
                        if (fl > 1) {
                            size_t i = fl - 1; while (i > 0 && full[i] != '/') --i;
                            if (i > 0) {
                                char parent[256]; size_t plen = (i < sizeof(parent)-2) ? i : (sizeof(parent)-2);
                                memcpy(parent, full, plen); parent[plen] = '\0'; ramfs_mkdir(parent);
                            }
                        }
                        ramfs_remove(full);
                        ramfs_create(full);
                        ramfs_write(full, out, off, 0);
                        if (!a->svc || !a->svc->redir_target) uart_puts("[svc] fallback write complete\n");
                        kfree(full);
                    }
                } else if (!a->svc || !a->svc->redir_target) {
                    uart_puts(out);
                }
            }
        }
    }
    /* mark service as stopped */
    if (a->svc) a->svc->pid = 0;
    /* disable future runs of this task (set fn to NULL) */
    int curid = task_current_id();
    if (curid > 0) task_set_fn_null(curid);
    /* free single allocated block (contains argv pointers + strings + struct) */
    if (a->mem) kfree(a->mem);
}

int services_init(void) {
    /* ensure directory exists */
    uart_puts("[svc] creating /etc...\n");
    ramfs_mkdir("/etc");
    uart_puts("[svc] creating systemd...\n");
    ramfs_mkdir("/etc/systemd");
    ramfs_mkdir("/etc/systemd/system");
    services = NULL;
    uart_puts("[svc] services initialized.\n");
    return 0;
}

int service_load_unit(const char *path) {
    if (!path) return -1;
    /* path expected like /etc/systemd/system/foo.service */
    char buf[4096];
    int r = ramfs_read(path, buf, sizeof(buf), 0);
    if (r < 0) return -1;
    /* find ExecStart= line */
    buf[r] = '\0';
    const char *p = strstr(buf, "ExecStart=");
    if (!p) return -1;
    p += strlen("ExecStart=");
    /* skip leading spaces */
    while (*p == ' ') ++p;
    /* copy command until newline */
    const char *e = p;
    while (*e && *e != '\n' && *e != '\r') ++e;
    size_t cmdlen = (size_t)(e - p);
    if (cmdlen == 0) return -1;
    char *cmd = kmalloc(cmdlen + 1);
    if (!cmd) return -1;
    memcpy(cmd, p, cmdlen); cmd[cmdlen] = '\0';

    /* detect simple redirection syntax: '... > target' or '... >> target' */
    char *redir_ptr = NULL; int append = 0;
    for (size_t i = 0; i < cmdlen; ++i) {
        if (cmd[i] == '>') {
            if (i + 1 < cmdlen && cmd[i+1] == '>') append = 1;
            redir_ptr = &cmd[i];
            break;
        }
    }
    char *target = NULL;
    if (redir_ptr) {
        /* terminate command before redirection */
        *redir_ptr = '\0';
        char *rt = redir_ptr + 1;
        if (*rt == '>') ++rt;
        while (*rt == ' ') ++rt;
        if (*rt) {
            size_t tlen = strlen(rt);
            /* strip trailing whitespace, quotes, and newlines */
            while (tlen > 0 && (rt[tlen-1] == '\n' || rt[tlen-1] == '\r' || rt[tlen-1] == ' ' || rt[tlen-1] == '"' || rt[tlen-1] == '\'')) {
                rt[tlen-1] = '\0';
                --tlen;
            }
            if (tlen > 0) {
                target = kmalloc(tlen + 1);
                if (target) memcpy(target, rt, tlen + 1);
            }
        }
    }

    /* derive service short name from path (filename without .service) */
    const char *last = path + strlen(path);
    while (last > path && *(last-1) != '/') --last;
    char name[SRV_NAME_MAX]; size_t nl = 0;
    const char *q = last;
    while (*q && nl + 1 < SRV_NAME_MAX) {
        if (*q == '.') break;
        name[nl++] = *q++;
    }
    name[nl] = '\0';

    /* if exists, replace exec string */
    struct service_entry *s = find_service(name);
    if (!s) {
        s = kmalloc(sizeof(*s));
        if (!s) { kfree(cmd); return -1; }
        memset(s, 0, sizeof(*s));
        strncpy(s->name, name, SRV_NAME_MAX-1);
        s->unit_path = kmalloc(strlen(path) + 1);
        memcpy(s->unit_path, path, strlen(path)+1);
        s->exec = cmd;
        s->redir_target = target;
        s->redir_append = append;
        s->enabled = 0;
        s->pid = 0;
        s->next = services;
        services = s;
    } else {
        if (s->exec) kfree(s->exec);
        s->exec = cmd;
        if (s->redir_target) kfree(s->redir_target);
        s->redir_target = target;
        s->redir_append = append;
        /* update unit_path if needed */
        if (!s->unit_path) {
            s->unit_path = kmalloc(strlen(path) + 1);
            memcpy(s->unit_path, path, strlen(path)+1);
        }
    }
    return 0;
}

int services_load_all(void) {
    char listbuf[1024];
    int r = ramfs_list("/etc/systemd/system", listbuf, sizeof(listbuf));
    if (r < 0) return -1;
    if (r == 0) return 0;
    /* entries separated by '\n' and terminated by '\0' */
    char *p = listbuf;
    while (*p) {
        /* build full path */
        char fname[128]; int i = 0;
        while (*p && *p != '\n' && i + 1 < (int)sizeof(fname)) fname[i++] = *p++;
        fname[i] = '\0';
        if (i > 0) {
            char full[256]; size_t pref = strlen("/etc/systemd/system/");
            if (pref + (size_t)i + 1 < sizeof(full)) {
                memcpy(full, "/etc/systemd/system/", pref);
                memcpy(full + pref, fname, (size_t)i);
                full[pref + i] = '\0';
                service_load_unit(full);
            }
        }
        if (*p == '\n') ++p;
    }
    return 0;
}

int service_start(const char *name) {
    struct service_entry *s = find_service(name);
    if (!s) {
        uart_puts("[svc] start failed: service not found: "); uart_puts(name); uart_puts("\n");
        return -1;
    }
    if (s->pid != 0) return 0; /* already running */
    if (!s->exec) {
        uart_puts("[svc] start failed: no exec string for: "); uart_puts(name); uart_puts("\n");
        return -1;
    }
    if (!s->redir_target) {
        uart_puts("[svc] starting: "); uart_puts(name); uart_puts("\n");
    }
    /* parse exec into argv (simple split by spaces) and allocate one block
       to avoid many small kmallocs which fragment the heap. Block layout:
       [struct svc_task_arg][argv pointers array][strings buffer]
    */
    const char *cmd = s->exec;
    size_t cmdlen = strlen(cmd);
    /* first count tokens */
    int argc = 0;
    const char *p = cmd;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        ++argc;
        while (*p && *p != ' ') ++p;
    }
    if (argc == 0) { uart_puts("service_start: no argv\n"); return -1; }
    size_t ptrs_size = sizeof(char*) * (argc + 1);
    size_t total = sizeof(struct svc_task_arg) + ptrs_size + (cmdlen + 1);
    void *block = kmalloc(total);
    if (!block) { uart_puts("service_start: oom argv array\n"); return -1; }
    struct svc_task_arg *arg = (struct svc_task_arg *)block;
    char **argv = (char **)(arg + 1);
    char *strbuf = (char *)((char *)argv + ptrs_size);
    /* copy command into strbuf and split in-place */
    memcpy(strbuf, cmd, cmdlen + 1);
    char *t = strbuf; int ai = 0;
    while (*t && ai < argc) {
        while (*t == ' ') ++t;
        if (!*t) break;
        argv[ai++] = t;
        while (*t && *t != ' ') ++t;
        if (*t == ' ') { *t = '\0'; ++t; }
    }
    argv[ai] = NULL;
    arg->argv = argv; arg->argc = argc; arg->svc = s; arg->mem = block;
    int pid = task_create(service_task_fn, arg, name);
    if (pid <= 0) {
        /* cleanup single block */
        if (block) kfree(block);
        uart_puts("service_start: task_create failed\n");
        return -1;
    }
    s->pid = pid;
    return pid;
}

int service_stop(const char *name) {
    struct service_entry *s = find_service(name);
    if (!s) return -1;
    if (s->pid == 0) return 0;
    int r = task_kill(s->pid);
    if (r == 0) s->pid = 0;
    return r;
}

int service_restart(const char *name) {
    service_stop(name);
    return service_start(name);
}

int service_reload(const char *name) {
    if (!name) {
        return services_load_all();
    }
    /* reload single unit and restart if running */
    struct service_entry *s = find_service(name);
    if (!s) {
        /* try to load it from expected path */
        char full[256];
        const char *pref = "/etc/systemd/system/";
        const char *suf = ".service";
        size_t plen = strlen(pref);
        size_t nlen = strlen(name);
        size_t slen = strlen(suf);
        if (plen + nlen + slen + 1 >= sizeof(full)) return -1;
        memcpy(full, pref, plen);
        memcpy(full + plen, name, nlen);
        memcpy(full + plen + nlen, suf, slen);
        full[plen + nlen + slen] = '\0';
        if (service_load_unit(full) < 0) return -1;
        s = find_service(name);
        if (!s) return -1;
    } else {
        /* reload unit file contents from unit_path */
        if (s->unit_path) service_load_unit(s->unit_path);
    }
    /* if running, restart */
    if (s->pid != 0) {
        return service_restart(name);
    }
    return 0;
}

int service_enable(const char *name) {
    struct service_entry *s = find_service(name);
    if (!s) {
        /* try to load it first */
        char full[256];
        const char *pref = "/etc/systemd/system/";
        const char *suf = ".service";
        size_t plen = strlen(pref);
        size_t nlen = strlen(name);
        size_t slen = strlen(suf);
        if (plen + nlen + slen + 1 < sizeof(full)) {
            memcpy(full, pref, plen);
            memcpy(full + plen, name, nlen);
            memcpy(full + plen + nlen, suf, slen);
            full[plen + nlen + slen] = '\0';
            service_load_unit(full);
        }
        s = find_service(name);
    }
    if (!s) return -1;
    s->enabled = 1;
    return 0;
}

int service_disable(const char *name) {
    struct service_entry *s = find_service(name);
    if (!s) {
        /* try to load it first */
        char full[256];
        const char *pref = "/etc/systemd/system/";
        const char *suf = ".service";
        size_t plen = strlen(pref);
        size_t nlen = strlen(name);
        size_t slen = strlen(suf);
        if (plen + nlen + slen + 1 < sizeof(full)) {
            memcpy(full, pref, plen);
            memcpy(full + plen, name, nlen);
            memcpy(full + plen + nlen, suf, slen);
            full[plen + nlen + slen] = '\0';
            service_load_unit(full);
        }
        s = find_service(name);
    }
    if (!s) return -1;
    s->enabled = 0;
    return 0;
}

int service_status(const char *name, char *buf, size_t len) {
    struct service_entry *s = find_service(name);
    if (!s) {
        const char *msg = "no such service\n";
        size_t m = strlen(msg); if (m >= len) m = len-1; if (m > 0) memcpy(buf, msg, m); if (len>0) buf[m]='\0'; return -1;
    }
    /* write simple status info */
    char tmp[256]; size_t p = 0;
    const char *label1 = "Name: ";
    const char *label2 = "Unit: ";
    const char *label3 = "Exec: ";
    const char *label4 = "Enabled: ";
    const char *label5 = "Active: ";
    size_t l;
    l = strlen(label1); if (p + l < sizeof(tmp)) { memcpy(tmp + p, label1, l); p += l; }
    l = strlen(s->name); if (p + l < sizeof(tmp)) { memcpy(tmp + p, s->name, l); p += l; }
    if (p + 1 < sizeof(tmp)) tmp[p++] = '\n';

    l = strlen(label2); if (p + l < sizeof(tmp)) { memcpy(tmp + p, label2, l); p += l; }
    const char *u = s->unit_path ? s->unit_path : "(none)"; l = strlen(u); if (p + l < sizeof(tmp)) { memcpy(tmp + p, u, l); p += l; }
    if (p + 1 < sizeof(tmp)) tmp[p++] = '\n';

    l = strlen(label3); if (p + l < sizeof(tmp)) { memcpy(tmp + p, label3, l); p += l; }
    const char *e = s->exec ? s->exec : "(none)"; l = strlen(e); if (p + l < sizeof(tmp)) { memcpy(tmp + p, e, l); p += l; }
    if (p + 1 < sizeof(tmp)) tmp[p++] = '\n';

    l = strlen(label4); if (p + l < sizeof(tmp)) { memcpy(tmp + p, label4, l); p += l; }
    const char *en = s->enabled ? "yes" : "no"; l = strlen(en); if (p + l < sizeof(tmp)) { memcpy(tmp + p, en, l); p += l; }
    if (p + 1 < sizeof(tmp)) tmp[p++] = '\n';

    l = strlen(label5); if (p + l < sizeof(tmp)) { memcpy(tmp + p, label5, l); p += l; }
    const char *ac = (s->pid != 0) ? "running" : "inactive"; l = strlen(ac); if (p + l < sizeof(tmp)) { memcpy(tmp + p, ac, l); p += l; }
    if (p + 1 < sizeof(tmp)) tmp[p++] = '\n';

    /* Redirect field removed for cleaner status output */

    size_t cp = p; if (cp >= len) cp = len - 1; if (cp > 0) memcpy(buf, tmp, cp); if (len > 0) buf[cp] = '\0';
    return (int)cp;
}
