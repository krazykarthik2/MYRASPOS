#include "uart.h"
#include "init.h"
#include "kmalloc.h"
#include "lib.h"
#include "sched.h"
#include "programs.h"
#include "glob.h"
#include <stddef.h>
#include <stdint.h>

/* forward prototype for external reference */
void shell_main(void *arg);

#define MAX_ARGS 8
#define MAX_CMDS 8
#define BUF_SIZE 2048

typedef int (*cmd_fn_t)(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);

/* Forward declarations of builtin command handlers */
static int cmd_help(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_echo(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_touch(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_write(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_cat(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_clear(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static size_t process_escapes(const char *src, char *dst, size_t dst_cap);
static int cmd_ps(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_sleep(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_wait(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_kill(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_ramfs_export(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_ramfs_import(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_systemctl(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_cd(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_pwd(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);

/* forward helpers */
static char *abs_path_alloc(const char *p);
static char *resolve_path_alloc(const char *p);
static char *normalize_abs_path_alloc(const char *path);
static int derive_service_shortname(const char *arg, char *out, size_t out_len, char *fullpath, size_t full_len);

struct cmd_entry { const char *name; cmd_fn_t fn; };

static struct cmd_entry commands[] = {
    {"help", cmd_help},
    {"clear", cmd_clear},
    {"echo", cmd_echo},
    {"cd", cmd_cd},
    {"pwd", cmd_pwd},
    {"touch", cmd_touch},
    {"write", cmd_write},
    {"cat", cmd_cat},
    {"sleep", cmd_sleep},
    {"wait", cmd_wait},
    {"kill", cmd_kill},
    {"ramfs-export", cmd_ramfs_export},
    {"ramfs-import", cmd_ramfs_import},
    {"systemctl", cmd_systemctl},
    {"ps", cmd_ps},
    {NULL, NULL},
};

/* current working directory for the shell (no concurrency expected) */
static char shell_cwd[256] = "/";

/* Exported helper for other kernel programs to resolve paths relative to the
   shell's current working directory. Returns kmalloc'd string; caller frees. */
char *init_resolve_path(const char *p) {
    return resolve_path_alloc(p);
}

/* Resolve a possibly-relative path to an allocated absolute path (caller must kfree)
   does NOT normalize '..' */
static char *resolve_path_alloc(const char *p) {
    if (!p) return NULL;
    if (p[0] == '/') {
        return normalize_abs_path_alloc(p);
    }
    /* relative -> join with shell_cwd and normalize */
    size_t clen = strlen(shell_cwd);
    size_t plen = strlen(p);
    /* build joined path string */
    if (strcmp(shell_cwd, "/") == 0) {
        /* avoid double slash: "/" + p -> "/p" */
        size_t need = 1 + plen + 1;
        char *tmp = kmalloc(need);
        if (!tmp) return NULL;
        tmp[0] = '/'; memcpy(tmp+1, p, plen+1);
        char *norm = normalize_abs_path_alloc(tmp);
        kfree(tmp);
        return norm;
    }
    size_t need = clen + 1 + plen + 1;
    char *tmp = kmalloc(need);
    if (!tmp) return NULL;
    memcpy(tmp, shell_cwd, clen);
    tmp[clen] = '/';
    memcpy(tmp + clen + 1, p, plen + 1);
    char *norm = normalize_abs_path_alloc(tmp);
    kfree(tmp);
    return norm;
}

/* Normalize an absolute path, resolving '.' and '..' components and collapsing
   repeated slashes. Returns a kmalloc'd normalized absolute path (starts with '/'). */
static char *normalize_abs_path_alloc(const char *path) {
    if (!path) return NULL;
    size_t len = strlen(path);
    char *out = kmalloc(len + 2);
    if (!out) return NULL;
    /* ensure path starts with '/' */
    size_t i = 0; size_t op = 0;
    if (len == 0) { out[0] = '/'; out[1] = '\0'; return out; }
    if (path[0] != '/') {
        /* make it absolute by prefixing slash */
        out[op++] = '/';
    }
    /* parse segments */
    size_t p = (path[0] == '/') ? 1 : 0;
    while (p <= len) {
        /* extract segment up to next slash */
        size_t j = p;
        while (j < len && path[j] != '/') ++j;
        size_t seglen = j - p;
        if (seglen == 0) {
            /* skip empty segment (consecutive slashes) */
        } else if (seglen == 1 && path[p] == '.') {
            /* skip '.' */
        } else if (seglen == 2 && path[p] == '.' && path[p+1] == '.') {
            /* go up one level if possible (remove last segment from out) */
            if (op > 1) {
                /* remove trailing slash if present */
                if (out[op-1] == '/') op--;
                /* remove last segment */
                while (op > 0 && out[op-1] != '/') op--;
            }
        } else {
            /* append segment */
            if (op == 0 || out[op-1] != '/') { out[op++] = '/'; }
            memcpy(out + op, path + p, seglen);
            op += seglen;
        }
        p = j + 1;
    }
    if (op == 0) { out[op++] = '/'; }
    out[op] = '\0';
    return out;
}

/* global interrupt flag set when user types Ctrl+C */
volatile int shell_sigint = 0;

static void shell_puts(const char *s) { init_puts(s); }

static int builtin_lookup(const char *name, cmd_fn_t *out) {
    for (int i = 0; commands[i].name; ++i) {
        if (strcmp(name, commands[i].name) == 0) { *out = commands[i].fn; return 0; }
    }
    return -1;
}

/* Read a line with basic editing */
static int shell_read_line(char *buf, size_t max) {
    size_t i = 0;
    while (i + 1 < max) {
        char c = uart_getc();
        if (c == '\r' || c == '\n') { shell_puts("\n"); break; }
        if (c == '\b' || c == 127) { if (i > 0) { i--; shell_puts("\b "); shell_puts("\b"); } continue; }
        char s[2] = {c, 0}; shell_puts(s); buf[i++] = c;
    }
    buf[i] = '\0';
    return (int)i;
}

/* Simple tokenizer splitting by spaces, does not handle quotes */
static int tokenize(char *s, char **argv, int max) {
    int argc = 0;
    while (*s && argc < max) {
        while (*s == ' ') ++s;
        if (!*s) break;
        argv[argc++] = s;
        while (*s && *s != ' ') ++s;
        if (*s == ' ') *s++ = '\0';
    }
    return argc;
}

/* (old exec_command removed; using exec_command_argv + program registry) */

/* Run a pipeline represented by array of command strings */
struct pipeline_job {
    int ncmds;
    char **argvs[MAX_CMDS];
    int argcs[MAX_CMDS];
    char *out_file; int append; int background;
    /* tokens allocated for the whole line (freed at end) */
    char **tokens;
    int token_count;
};

static int exec_command_argv(char **argv, int argc, const char *in, size_t in_len, char *out, size_t out_cap) {
    if (argc == 0) return 0;
    cmd_fn_t fn;
    if (builtin_lookup(argv[0], &fn) == 0) {
        return fn(argc, argv, in, in_len, out, out_cap);
    }
    prog_fn_t pfn;
    if (program_lookup(argv[0], &pfn) == 0) {
        return pfn(argc, argv, in, in_len, out, out_cap);
    }
    const char *msg = "unknown command\n";
    size_t m = strlen(msg);
    if (m > out_cap) m = out_cap;
    memcpy(out, msg, m);
    return (int)m;
}

static void run_pipeline_internal(struct pipeline_job *job) {
    /* clear any pending SIGINT for this run */
    shell_sigint = 0;
    char *inbuf = NULL;
    size_t in_len = 0;
    char *bufs[MAX_CMDS];
    for (int i = 0; i < job->ncmds; ++i) bufs[i] = NULL;

    for (int i = 0; i < job->ncmds; ++i) {
        bufs[i] = kmalloc(BUF_SIZE);
        if (!bufs[i]) break;
        int wrote = exec_command_argv(job->argvs[i], job->argcs[i], inbuf, in_len, bufs[i], BUF_SIZE);
        if (inbuf) kfree(inbuf);
        inbuf = bufs[i];
        in_len = (wrote > 0) ? (size_t)wrote : 0;
        /* check for Ctrl+C requested by user and abort pipeline */
        if (uart_haschar()) {
            char ch = uart_getc();
            if (ch == 3) { /* Ctrl+C */
                shell_sigint = 1;
                break;
            }
        }
        if (shell_sigint) break;
    }

    /* final output handling */
    if (inbuf) {
        if (job->out_file) {
            /* ensure file exists (create if needed) */
            init_ramfs_create(job->out_file); /* ignore error if exists */
            /* write to file via init API */
            init_ramfs_write(job->out_file, inbuf, in_len, job->append);
        } else {
            /* print to console */
            size_t off = 0;
            while (off < in_len) {
                size_t to = in_len - off;
                if (to > 128) to = 128;
                char tbuf[129];
                memcpy(tbuf, inbuf + off, to);
                tbuf[to] = '\0';
                init_puts(tbuf);
                off += to;
            }
        }
    }

    for (int i = 0; i < job->ncmds; ++i) if (bufs[i]) kfree(bufs[i]);
}

static void pipeline_runner(void *arg) {
    struct pipeline_job *job = (struct pipeline_job *)arg;
    run_pipeline_internal(job);
    /* free job memory */
    for (int i = 0; i < job->ncmds; ++i) {
        if (job->argvs[i]) kfree(job->argvs[i]);
    }
    for (int t = 0; t < job->token_count; ++t) if (job->tokens[t]) kfree(job->tokens[t]);
    if (job->tokens) kfree(job->tokens);
    if (job->out_file) kfree(job->out_file);
    kfree(job);
    /* don't free the task here; the scheduler will keep the struct but we
       disable future runs by setting its fn to NULL via helper */
    int curid = task_current_id();
    if (curid > 0) task_set_fn_null(curid);
}

/* wrapper used for background tasks: run pipeline then disable further runs */
static void background_wrapper(void *arg) {
    pipeline_runner(arg);
    /* pipeline_runner will have set fn to NULL for this task already, but
       call again to be safe */
    int id = task_current_id();
    if (id > 0) task_set_fn_null(id);
}

/* Parse a line into pipeline_job structure (supports |, >, >>, & at end) */
static struct pipeline_job *parse_pipeline(const char *line_in) {
    size_t L = strlen(line_in);
    char *buf = kmalloc(L + 1);
    if (!buf) return NULL;
    memcpy(buf, line_in, L + 1);

    /* tokenize respecting quotes and escapes */
    char *tokens[64];
    int tcount = 0;
    size_t i = 0;
    while (i < L) {
        /* skip whitespace */
        while (i < L && (buf[i] == ' ' || buf[i] == '\t')) ++i;
        if (i >= L) break;
        char c = buf[i];
        if (c == '"' || c == '\'') {
            char q = c; ++i; size_t start = i;
            while (i < L && buf[i] != q) {
                if (buf[i] == '\\' && i + 1 < L) { /* escape */
                    ++i; /* skip backslash, next char copied */
                }
                ++i;
            }
            size_t len = i - start;
            char *tok = kmalloc(len + 1);
            if (!tok) break;
            /* copy with handling escapes -- preserve backslashes so '-e' can interpret them */
            size_t di = 0;
            for (size_t j = start; j < start + len; ++j) {
                if (buf[j] == '\\' && j + 1 < start + len) {
                    /* keep the backslash and the escaped char */
                    tok[di++] = '\\';
                    ++j;
                    tok[di++] = buf[j];
                    continue;
                }
                tok[di++] = buf[j];
            }
            tok[di] = '\0';
            tokens[tcount++] = tok;
            if (i < L && buf[i] == q) ++i; /* skip closing quote */
        } else if (c == '|' || c == '&' || c == '>') {
            if (c == '>' && i + 1 < L && buf[i+1] == '>') {
                char *tok = kmalloc(3); tok[0] = '>'; tok[1] = '>'; tok[2] = '\0'; tokens[tcount++] = tok; i += 2;
            } else {
                char *tok = kmalloc(2); tok[0] = c; tok[1] = '\0'; tokens[tcount++] = tok; ++i;
            }
        } else {
            size_t start = i;
            while (i < L && buf[i] != ' ' && buf[i] != '\t' && buf[i] != '|' && buf[i] != '&' && buf[i] != '>') {
                if (buf[i] == '\\' && i + 1 < L) { ++i; }
                ++i;
            }
            size_t len = i - start;
            char *tok = kmalloc(len + 1);
            if (!tok) break;
            size_t di = 0;
            for (size_t j = start; j < start + len; ++j) {
                if (buf[j] == '\\' && j + 1 < start + len) {
                    tok[di++] = '\\';
                    ++j;
                    tok[di++] = buf[j];
                    continue;
                }
                tok[di++] = buf[j];
            }
            tok[di] = '\0';
            tokens[tcount++] = tok;
        }
    }

    struct pipeline_job *job = kmalloc(sizeof(*job));
    if (!job) {
        for (int k = 0; k < tcount; ++k) kfree(tokens[k]);
        kfree(buf);
        return NULL;
    }
    job->ncmds = 0; job->out_file = NULL; job->append = 0; job->background = 0;
    job->tokens = kmalloc(sizeof(char*) * tcount);
    job->token_count = tcount;
    for (int k = 0; k < tcount; ++k) job->tokens[k] = tokens[k];

    /* build commands from tokens */
    int idx = 0;
    while (idx < tcount && job->ncmds < MAX_CMDS) {
        /* check for background marker at end */
        if (idx == tcount-1 && strcmp(tokens[idx], "&") == 0) { job->background = 1; break; }

        /* build argv for this command until | or end or redirection */
        char *argvs[MAX_ARGS]; int argc = 0;
        while (idx < tcount) {
            char *tk = job->tokens[idx];
            if (strcmp(tk, "|") == 0) { ++idx; break; }
            if (strcmp(tk, ">") == 0 || strcmp(tk, ">>") == 0) {
                job->append = (strcmp(tk, ">>") == 0);
                ++idx;
                if (idx < tcount) {
                    /* resolve redirection target relative to cwd */
                    char *raw = job->tokens[idx];
                    char *resolved = init_resolve_path(raw);
                    if (resolved) {
                        job->out_file = resolved;
                        /* free original token memory; pipeline_runner will skip double-free */
                        kfree(raw);
                        job->tokens[idx] = NULL;
                    } else {
                        job->out_file = kmalloc(strlen(raw) + 1);
                        if (job->out_file) memcpy(job->out_file, raw, strlen(raw) + 1);
                    }
                    ++idx;
                }
                /* redirection ends command parsing */
                /* consume remaining tokens until pipe or end */
                while (idx < tcount && strcmp(job->tokens[idx], "|") != 0) ++idx;
                if (idx < tcount && strcmp(job->tokens[idx], "|") == 0) ++idx;
                break;
            }
            /* normal token -> add to argv */
            if (argc < MAX_ARGS) argvs[argc++] = tk;
            ++idx;
        }
        /* allocate argv array and copy pointers */
        char **argvp = kmalloc(sizeof(char*) * (argc + 1));
        for (int a = 0; a < argc; ++a) argvp[a] = argvs[a];
        argvp[argc] = NULL;
        job->argvs[job->ncmds] = argvp;
        job->argcs[job->ncmds] = argc;
        job->ncmds++;
    }

    kfree(buf);
    return job;
}

/* Builtin implementations */
static int cmd_help(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)argc; (void)argv; (void)in; (void)in_len;
    const char *msg = "help:";
    for(int i=0; commands[i].name; ++i) {
        size_t l = strlen(msg);
        if (l + 1 < out_cap) {
            memcpy((char *)msg + l, "\n", 1);
            memcpy((char *)msg + l + 1, commands[i].name, strlen(commands[i].name));
            ((char *)msg)[l + 1 + strlen(commands[i].name)] = '\0';
        }
    }
    ((char *)msg)[strlen(msg)] = '\n';
    size_t m = strlen(msg); if (m > out_cap) m = out_cap; memcpy(out, msg, m); return (int)m;
}

static int cmd_echo(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    size_t off = 0;
    int start = 1;
    int interpret = 0;
    if (argc >= 2 && strcmp(argv[1], "-e") == 0) { interpret = 1; start = 2; }
    for (int i = start; i < argc; ++i) {
        if (interpret) {
            /* process escapes into a temporary small buffer then copy */
            char tmp[256]; size_t w = process_escapes(argv[i], tmp, sizeof(tmp));
            if (off + w >= out_cap) { size_t can = out_cap - off; memcpy(out+off, tmp, can); off += can; break; }
            memcpy(out+off, tmp, w); off += w;
        } else {
            size_t l = strlen(argv[i]);
            if (off + l >= out_cap) break;
            memcpy(out+off, argv[i], l);
            off += l;
        }
        if (i + 1 < argc) {
            if (off + 1 >= out_cap) break;
            out[off++] = ' ';
        }
    }
    if (off < out_cap) out[off++] = '\n';
    return (int)off;
}

static int cmd_touch(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 2) { const char *u = "usage: touch <name>\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m; }
    char *ap = init_resolve_path(argv[1]);
    if (!ap) { const char *m = "fail\n"; size_t mm = strlen(m); if (mm>out_cap) mm=out_cap; memcpy(out,m,mm); return (int)mm; }
    int r = init_ramfs_create(ap);
    kfree(ap);
    const char *msg = (r==0)?"ok\n":"fail\n"; size_t m = strlen(msg); if (m>out_cap) m=out_cap; memcpy(out,msg,m); return (int)m;
}

static int cmd_write(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 3) { const char *u = "usage: write <name> <text>\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m; }
    char *an = init_resolve_path(argv[1]);
    if (!an) { const char *m = "fail\n"; size_t mm = strlen(m); if (mm>out_cap) mm=out_cap; memcpy(out,m,mm); return (int)mm; }
    const char *name = an;
    /* join remaining args into text */
    size_t total = 0;
    for (int i = 2; i < argc; ++i) total += strlen(argv[i]) + 1;
    char *text = kmalloc(total+1);
    if (!text) return -1;
    size_t off = 0; for (int i = 2; i < argc; ++i) { size_t l = strlen(argv[i]); memcpy(text+off, argv[i], l); off += l; if (i+1<argc) text[off++]=' '; }
    text[off]=0;
    init_ramfs_remove(name);
    init_ramfs_create(name);
    int w = init_ramfs_write(name, text, off, 0);
    kfree(text);
    kfree(an);
    const char *msg = (w>=0)?"wrote\n":"fail\n"; size_t m = strlen(msg); if (m>out_cap) m=out_cap; memcpy(out,msg,m); return (int)m;
}

static int cmd_cd(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 2) {
        /* cd with no args -> root */
        strcpy(shell_cwd, "/");
        const char *s = "ok\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
    }
    const char *arg = argv[1];
    /* if arg is '.' -> stay */
    if (strcmp(arg, ".") == 0) { const char *s = "ok\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m; }
    /* if arg is '..' -> parent */
    if (strcmp(arg, "..") == 0) {
        if (strcmp(shell_cwd, "/") == 0) { const char *s = "ok\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m; }
        /* remove last path component */
        char tmp[256]; strncpy(tmp, shell_cwd, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = '\0';
        size_t l = strlen(tmp);
        if (l > 1 && tmp[l-1] == '/') tmp[l-1] = '\0';
        while (l > 0 && tmp[l-1] != '/') { tmp[--l] = '\0'; }
        if (l == 0) strcpy(shell_cwd, "/"); else strncpy(shell_cwd, tmp, sizeof(shell_cwd)-1);
        const char *s = "ok\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
    }

    /* handle glob patterns in cd: if pattern matches exactly one directory, cd into it */
    int has_glob = 0;
    for (const char *pp = arg; *pp; ++pp) { if (*pp=='*' || *pp=='?' || *pp=='[') { has_glob = 1; break; } }
    if (has_glob) {
        /* split directory and pattern */
        const char *last = arg + strlen(arg);
        while (last > arg && *(last-1) != '/') --last;
        char dir[128]; char pat[128];
        if (last == arg) { strcpy(dir, "."); strncpy(pat, arg, sizeof(pat)-1); pat[sizeof(pat)-1] = '\0'; }
        else {
            size_t dlen = (size_t)(last - arg);
            if (dlen >= sizeof(dir)) dlen = sizeof(dir)-1;
            memcpy(dir, arg, dlen); dir[dlen] = '\0';
            strncpy(pat, last, sizeof(pat)-1); pat[sizeof(pat)-1] = '\0';
        }
        char *rdir = init_resolve_path(dir);
        if (!rdir) { const char *f = "fail\n"; size_t m = strlen(f); if (m>out_cap) m=out_cap; memcpy(out,f,m); return (int)m; }
        char listbuf[1024]; int rc = init_ramfs_list(rdir, listbuf, sizeof(listbuf));
        if (rc < 0) { kfree(rdir); const char *f = "fail\n"; size_t m = strlen(f); if (m>out_cap) m=out_cap; memcpy(out,f,m); return (int)m; }
        int matches = 0; char matched[128];
        size_t p = 0;
        while (p < (size_t)rc) {
            size_t l = 0; while (p + l < (size_t)rc && listbuf[p + l] != '\n') ++l;
            if (l == 0) break;
            const char *name = &listbuf[p];
            if (glob_match(pat, name)) {
                /* ensure it's a directory (ends with '/') */
                if (l > 0 && name[l-1] == '/') {
                    if (matches == 0) { size_t ml = (l < sizeof(matched)-1) ? l : (sizeof(matched)-1); memcpy(matched, name, ml); matched[ml] = '\0'; }
                    matches++;
                }
            }
            p += l + 1;
        }
        if (matches == 1) {
            /* build full path: rdir + matched */
            size_t rlen = strlen(rdir); size_t mlen = strlen(matched);
            char *full = kmalloc(rlen + 1 + mlen + 1);
            if (!full) { kfree(rdir); const char *f = "fail\n"; size_t m = strlen(f); if (m>out_cap) m=out_cap; memcpy(out,f,m); return (int)m; }
            if (strcmp(rdir, "/") == 0) { full[0] = '\0'; }
            else { memcpy(full, rdir, rlen); full[rlen] = '\0'; }
            if (strcmp(rdir, "/") != 0) {
                size_t cur = strlen(full);
                full[cur++] = '/';
                memcpy(full + cur, matched, mlen);
                full[cur + mlen] = '\0';
            } else {
                /* rdir == "/"; matched already relative */
                memcpy(full + 1, matched, mlen + 1);
                full[0] = '/';
            }
            /* normalize and set cwd */
            char *norm = resolve_path_alloc(full);
            if (norm) {
                if (strcmp(norm, "/") == 0) strcpy(shell_cwd, "/"); else {
                    size_t l = strlen(norm); if (l > 1 && norm[l-1] == '/') norm[l-1] = '\0'; strncpy(shell_cwd, norm, sizeof(shell_cwd)-1); shell_cwd[sizeof(shell_cwd)-1] = '\0'; }
                kfree(norm);
            }
            kfree(full);
            kfree(rdir);
            const char *s = "ok\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
        } else if (matches > 1) {
            kfree(rdir);
            const char *f = "cd: too many matches\n"; size_t m = strlen(f); if (m>out_cap) m=out_cap; memcpy(out,f,m); return (int)m;
        } else {
            kfree(rdir);
            const char *f = "cd: no such directory\n"; size_t m = strlen(f); if (m>out_cap) m=out_cap; memcpy(out,f,m); return (int)m;
        }
    }

    char *abs = resolve_path_alloc(argv[1]);
    if (!abs) { const char *f = "fail\n"; size_t m = strlen(f); if (m>out_cap) m=out_cap; memcpy(out,f,m); return (int)m; }
    /* verify directory exists */
    char buf[128]; int r = init_ramfs_list(abs, buf, sizeof(buf));
    if (r < 0) { kfree(abs); const char *f = "fail\n"; size_t m = strlen(f); if (m>out_cap) m=out_cap; memcpy(out,f,m); return (int)m; }
    /* normalize: keep '/' as root, else strip trailing slash */
    if (strcmp(abs, "/") == 0) {
        strcpy(shell_cwd, "/");
    } else {
        size_t l = strlen(abs);
        /* remove trailing slash if present */
        if (l > 1 && abs[l-1] == '/') abs[l-1] = '\0';
        if (strlen(abs) < sizeof(shell_cwd)) strcpy(shell_cwd, abs);
        else shell_cwd[0] = '\0';
    }
    kfree(abs);
    const char *s = "ok\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
}

static int cmd_pwd(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)argc; (void)argv; (void)in; (void)in_len;
    size_t l = strlen(shell_cwd);
    if (l + 1 >= out_cap) l = out_cap - 1;
    memcpy(out, shell_cwd, l);
    out[l++] = '\n';
    if (l < out_cap) out[l] = '\0';
    return (int)l;
}

static int cmd_ramfs_export(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 2) { const char *u = "usage: ramfs-export <path>\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m; }
    char *ap = abs_path_alloc(argv[1]);
    if (!ap) { const char *s = "failed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m; }
    int r = init_ramfs_export(ap);
    kfree(ap);
    const char *s = (r==0) ? "exported\n" : "failed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
}

static int cmd_ramfs_import(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 2) { const char *u = "usage: ramfs-import <path>\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m; }
    char *ap = abs_path_alloc(argv[1]);
    if (!ap) { const char *s = "failed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m; }
    int r = init_ramfs_import(ap);
    kfree(ap);
    const char *s = (r==0) ? "imported\n" : "failed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
}

/* Helper: return an allocated absolute path (prefix '/' if missing). Caller must kfree. */
static char *abs_path_alloc(const char *p) {
    if (!p) return NULL;
    return normalize_abs_path_alloc(p);
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

static int cmd_cat(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 2) { const char *u = "usage: cat <name>\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m; }
    int r = init_ramfs_read(argv[1], out, out_cap);
    if (r < 0) { const char *f = "fail\n"; size_t m = strlen(f); if (m>out_cap) m=out_cap; memcpy(out,f,m); return (int)m; }
    return r;
}

/* process common backslash escapes into dst, return bytes written (no NUL) */
static size_t process_escapes(const char *src, char *dst, size_t dst_cap) {
    size_t di = 0;
    for (const char *p = src; *p && di < dst_cap; ++p) {
        if (*p == '\\' && *(p+1)) {
            ++p;
            char c = *p;
            char outc = c;
            if (c == 'n') outc = '\n';
            else if (c == 't') outc = '\t';
            else if (c == 'r') outc = '\r';
            else if (c == '\\') outc = '\\';
            else if (c == '0') outc = '\0';
            else outc = c;
            if (di < dst_cap) dst[di++] = outc;
        } else {
            dst[di++] = *p;
        }
    }
    return di;
}

static int cmd_clear(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)argc; (void)argv; (void)in; (void)in_len; (void)out; (void)out_cap;
    /* ANSI clear screen + move cursor home */
    init_puts("\x1b[2J\x1b[H");
    return 0;
}
/* Builtin: sleep seconds (cooperative) */
static int cmd_sleep(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len; (void)out; (void)out_cap;
    if (argc < 2) return 0;
    /* parse integer seconds */
    int sec = 0; for (char *p = argv[1]; *p; ++p) { if (*p >= '0' && *p <= '9') sec = sec*10 + (*p - '0'); }
    /* coarse-grained sleep by yielding, check for Ctrl+C */
    int ticks = sec * 50; /* arbitrary ticks per second */
    for (int i = 0; i < ticks; ++i) {
        if (uart_haschar()) { char ch = uart_getc(); if (ch == 3) { shell_sigint = 1; break; } }
        if (shell_sigint) break;
        yield();
    }
    return shell_sigint ? -1 : 0;
}

/* Builtin: wait <pid> - wait until task disappears */
static int cmd_wait(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len; (void)out; (void)out_cap;
    if (argc < 2) return 0;
    int pid = 0; for (char *p = argv[1]; *p; ++p) { if (*p >= '0' && *p <= '9') pid = pid*10 + (*p - '0'); }
    while (task_exists(pid)) yield();
    return 0;
}

/* Builtin: kill <pid> */
static int cmd_kill(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
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

/* Builtin: systemctl <cmd> [name]
   supported: status, start, stop, restart, reload, enable, disable
 */
static int cmd_systemctl(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    if (argc < 2) {
        const char *u = "usage: systemctl <status|start|stop|restart|reload|enable|disable> [name]\n";
        size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m;
    }
    const char *cmd = argv[1];
    const char *name = (argc >= 3) ? argv[2] : NULL;
    if (strcmp(cmd, "status") == 0) {
        if (!name) { const char *u = "usage: systemctl status <name>\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m; }
        char shortname[64]; char full[256];
        derive_service_shortname(name, shortname, sizeof(shortname), full, sizeof(full));
        int r = init_service_status(shortname, out, out_cap);
        return r;
    } else if (strcmp(cmd, "start") == 0) {
        if (!name) { const char *u = "usage: systemctl start <name>\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m; }
        char shortname[64]; char full[256];
        derive_service_shortname(name, shortname, sizeof(shortname), full, sizeof(full));
        int r = init_service_start(shortname);
        if (r < 0) {
            /* maybe unit not loaded yet: try loading from full path (if available) and retry */
            if (full[0]) init_service_load_unit(full);
            r = init_service_start(shortname);
        }
        if (r > 0) {
            char b[64]; int bl = 0;
            int tmp = r; char tbuf[16]; int tl = 0; if (tmp==0) { tbuf[tl++]='0'; } else { int digs[12]; int di=0; while (tmp>0 && di<12) { digs[di++]=tmp%10; tmp/=10; } for (int j=di-1;j>=0;--j) tbuf[tl++]= '0' + digs[j]; }
            for (int i=0;i<tl;++i) b[bl++]=tbuf[i]; b[bl++]='\n'; if (bl>out_cap) bl=out_cap; memcpy(out,b,bl); return bl;
        } else {
            const char *s = "failed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
        }
    } else if (strcmp(cmd, "stop") == 0) {
        if (!name) { const char *u = "usage: systemctl stop <name>\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m; }
        int r = init_service_stop(name);
        const char *s = (r==0) ? "stopped\n" : "failed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
    } else if (strcmp(cmd, "restart") == 0) {
        if (!name) { const char *u = "usage: systemctl restart <name>\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m; }
        char shortname[64]; char full[256]; derive_service_shortname(name, shortname, sizeof(shortname), full, sizeof(full));
        int r = init_service_restart(shortname);
        const char *s = (r==0) ? "ok\n" : "failed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
    } else if (strcmp(cmd, "reload") == 0) {
        int r;
        if (name) {
            char shortname[64]; char full[256]; derive_service_shortname(name, shortname, sizeof(shortname), full, sizeof(full));
            r = init_service_reload(shortname);
        } else r = init_service_reload(NULL);
        const char *s = (r==0) ? "reloaded\n" : "failed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
    } else if (strcmp(cmd, "enable") == 0) {
        if (!name) { const char *u = "usage: systemctl enable <name>\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m; }
        char shortname[64]; char full[256]; derive_service_shortname(name, shortname, sizeof(shortname), full, sizeof(full));
        int r = init_service_enable(shortname);
        const char *s = (r==0) ? "enabled\n" : "failed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
    } else if (strcmp(cmd, "disable") == 0) {
        if (!name) { const char *u = "usage: systemctl disable <name>\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m; }
        char shortname[64]; char full[256]; derive_service_shortname(name, shortname, sizeof(shortname), full, sizeof(full));
        int r = init_service_disable(shortname);
        const char *s = (r==0) ? "disabled\n" : "failed\n"; size_t m = strlen(s); if (m>out_cap) m=out_cap; memcpy(out,s,m); return (int)m;
    }
    const char *u = "unknown subcommand\n"; size_t m = strlen(u); if (m>out_cap) m=out_cap; memcpy(out,u,m); return (int)m;
}

/* Builtin: ps - list task ids */
static int cmd_ps(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)argc; (void)argv; (void)in; (void)in_len;
    int ids[32]; int runs[32]; int starts[32]; int total_runs = 0;
    int n = task_stats(ids, runs, starts, 32, &total_runs);
    size_t off = 0;
    /* header */
    const char *hdr = "PID   %CPU  %MEM  VSZ   RSS   TTY    STAT START  TIME     COMMAND\n";
    size_t hlen = strlen(hdr); if (hlen > out_cap) hlen = out_cap; memcpy(out+off, hdr, hlen); off += hlen;
    for (int i = 0; i < n; ++i) {
        if (off + 64 > out_cap) break;
        int pid = ids[i];
        int run = runs[i];
        /* compute %CPU = (run / total_runs) * 100.0 as one decimal */
        int cpu_per_tenth = 0;
        if (total_runs > 0) cpu_per_tenth = (run * 1000) / total_runs; /* tenths of percent */
        int cpu_int = cpu_per_tenth / 10;
        int cpu_frac = cpu_per_tenth % 10;
        /* %MEM/VSZ/RSS unknown in this simple kernel => 0 */
        int mem_int = 0; int vsz = 0; int rss = 0;
        const char *tty = "ttyS0";
        const char *stat = (run > 0) ? "R" : "S";
        int start = starts[i];
        /* TIME in seconds = run / 50 (ticks per second) */
        int secs = (total_runs>0) ? (run / 50) : 0;
        int mins = secs / 60; int srem = secs % 60;
        /* build line manually */
        char line[128]; int p = 0;
        /* PID (width 5) */
        int t = pid; char num[16]; int nl = 0; if (t==0) num[nl++]='0'; else { int digs[12]; int di=0; while (t>0 && di<12) { digs[di++]=t%10; t/=10; } for (int j=di-1;j>=0;--j) num[nl++]=(char)('0'+digs[j]); }
        /* pad to 5 */
        int pad = 5 - nl; for (int k=0;k<pad;++k) line[p++]=' ';
        for (int k=0;k<nl;++k) line[p++]=num[k]; line[p++]=' ';
        /* %CPU format like 12.3 (one decimal) */
        {
            char cbuf[8]; int ci = 0;
            int tmp = cpu_int;
            if (tmp == 0) { cbuf[ci++] = '0'; }
            else { int digs[8]; int di = 0; while (tmp > 0 && di < 8) { digs[di++] = tmp % 10; tmp /= 10; } for (int j = di - 1; j >= 0; --j) cbuf[ci++] = (char)('0' + digs[j]); }
            cbuf[ci++] = '.'; cbuf[ci++] = (char)('0' + cpu_frac); cbuf[ci] = '\0';
            /* pad to width 6 (approx) */
            int clen = ci;
            int padc = 6 - clen; for (int k=0;k<padc;++k) line[p++]=' ';
            for (int k=0;k<clen;++k) line[p++]=cbuf[k];
            line[p++]=' ';
        }
        /* %MEM */
        line[p++]=' '; line[p++]='0'; line[p++]='.'; line[p++]='0'; line[p++]=' ';
        /* VSZ */
        line[p++]=' '; line[p++]='0'; line[p++]=' ';
        /* RSS */
        line[p++]=' '; line[p++]='0'; line[p++]=' ';
        /* TTY */
        int tn = strlen(tty); for (int k=0;k<tn;++k) line[p++]=tty[k]; for (int k=0;k<6-tn;++k) line[p++]=' ';
        line[p++]=' ';
        /* STAT */
        line[p++]=stat[0]; for (int k=0;k<4-1;++k) line[p++]=' ';
        /* START (print start tick) */
        /* convert start to digits */
        int st = start; char sn[16]; int sl=0; if (st==0) sn[sl++]='0'; else { int digs[12]; int di=0; while (st>0 && di<12) { digs[di++]=st%10; st/=10; } for (int j=di-1;j>=0;--j) sn[sl++]=(char)('0'+digs[j]); }
        int spad = 6 - sl; for (int k=0;k<spad;++k) line[p++]=' ';
        for (int k=0;k<sl;++k) line[p++]=sn[k]; line[p++]=' ';
        /* TIME mm:ss */
        char tm[8]; int tp=0; /* mm */
        if (mins >= 10) { int m1 = mins/10; int m2 = mins%10; tm[tp++]=(char)('0'+m1); tm[tp++]=(char)('0'+m2); }
        else { tm[tp++]='0'; tm[tp++]=(char)('0'+mins); }
        tm[tp++]=':'; tm[tp++]=(char)('0'+(srem/10)); tm[tp++]=(char)('0'+(srem%10)); tm[tp]='\0';
        int tml = tp;
        for (int k=0;k<7-tml;++k) line[p++]=' ';
        for (int k=0;k<tml;++k) line[p++]=tm[k]; line[p++]=' ';
        /* COMMAND: task<pid> */
        {
            line[p++]='t'; line[p++]='a'; line[p++]='s'; line[p++]='k';
            int tid = pid; char idbuf[16]; int il=0; if (tid==0) idbuf[il++]='0'; else { int digs[12]; int di=0; while (tid>0 && di<12) { digs[di++]=tid%10; tid/=10; } for (int j=di-1;j>=0;--j) idbuf[il++]=(char)('0'+digs[j]); }
            for (int k=0;k<il;++k) line[p++]=idbuf[k];
            line[p++]='\n';
        }

        /* copy line to out */
        if (off + p > out_cap) break;
        memcpy(out + off, line, p); off += p;
    }
    return (int)off;
}

/* shell main */
void shell_main(void *arg) {
    (void)arg;
    shell_puts("myras shell v0.2\nType 'help' for commands.\n");
    char line[256];
    for (;;) {
        /* print prompt showing current cwd: myras::/path$ */
        char pbuf[320]; size_t pl = 0;
        const char *prefix = "myras::";
        size_t prelen = strlen(prefix);
        if (prelen + strlen(shell_cwd) + 3 < sizeof(pbuf)) {
            memcpy(pbuf, prefix, prelen); pl += prelen;
            size_t cwdlen = strlen(shell_cwd);
            memcpy(pbuf + pl, shell_cwd, cwdlen); pl += cwdlen;
            pbuf[pl++] = '$'; pbuf[pl++] = ' ';
            pbuf[pl] = '\0';
            shell_puts(pbuf);
        } else {
            shell_puts("myras> ");
        }
        int len = shell_read_line(line, sizeof(line));
        if (len <= 0) { yield(); continue; }
        struct pipeline_job *job = parse_pipeline(line);
        if (!job) { shell_puts("error parsing\n"); continue; }
        if (job->background) {
            /* create a background wrapper task that runs once and then disables itself */
            int pid = task_create(background_wrapper, job);
            /* print background pid */
            char bbuf[32]; int bl = 0;
            int tmp = pid; if (tmp==0) { bbuf[bl++]='0'; }
            else { int p10[16]; int pi=0; while (tmp>0 && pi<16) { p10[pi++]=tmp%10; tmp/=10; } for (int j=pi-1;j>=0;--j) bbuf[bl++]= '0' + p10[j]; }
            bbuf[bl++]='\n'; bbuf[bl]=0; init_puts("started pid "); init_puts(bbuf);
        } else {
            /* run pipeline directly in the shell task (foreground). Poll for Ctrl+C between commands. */
            /* clear interrupt flag */
            extern volatile int shell_sigint;
            shell_sigint = 0;
            run_pipeline_internal(job);
            /* free job (same as pipeline_runner cleanup) */
            for (int i = 0; i < job->ncmds; ++i) {
                if (job->argvs[i]) kfree(job->argvs[i]);
            }
            for (int t = 0; t < job->token_count; ++t) if (job->tokens[t]) kfree(job->tokens[t]);
            if (job->tokens) kfree(job->tokens);
            if (job->out_file) kfree(job->out_file);
            kfree(job);
        }
    }
}
