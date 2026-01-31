#include "uart.h"
#include "init.h"
#include "sched.h"
#include "kmalloc.h"
#include "lib.h"
#include "sched.h"
#include "programs.h"
#include "glob.h"
#include "pty.h"
#include <stddef.h>
#include <stdint.h>
#include "palloc.h"
#include "shell.h"
#include "apps/image_viewer.h"

/* forward prototype for external reference */
void shell_main(void *arg);

#define MAX_ARGS 8
#define MAX_CMDS 8
#define BUF_SIZE 2048

typedef int (*cmd_fn_t)(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);

/* Forward declarations of builtin command handlers */
static int cmd_cd(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_pwd(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);
static int cmd_exit(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap);

/* forward helpers */
static char *resolve_path_alloc(const char *p);
static char *normalize_abs_path_alloc(const char *path);

struct cmd_entry { const char *name; cmd_fn_t fn; };
static struct cmd_entry commands[] = {
    {"cd", cmd_cd},
    {"pwd", cmd_pwd},
    {"exit", cmd_exit},
    {NULL, NULL},
};

static int shell_should_exit = 0;


static struct pipeline_job *parse_pipeline(const char *line_in);
static void run_pipeline_internal(struct pipeline_job *job);

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
    size_t op = 0;
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
        char c = init_getc();
        if (c == 0) { yield(); continue; }
        if (c == '\r' || c == '\n') { shell_puts("\n"); break; }
        if (c == '\b' || c == 127) { if (i > 0) { i--; shell_puts("\b "); shell_puts("\b"); } continue; }
        char s[2] = {c, 0}; shell_puts(s); buf[i++] = c;
    }
    buf[i] = '\0';
    return (int)i;
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
    struct pty *pty;
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
        if (inbuf) { kfree(inbuf); }
        /* Mark previous bufs as NULL so we don't double-free in the final cleanup */
        if (i > 0) bufs[i-1] = NULL; 

        inbuf = bufs[i];
        in_len = (wrote > 0) ? (size_t)wrote : 0;
        /* check for interrupt flag */
        if (shell_sigint) break;
        yield();
    }

    /* final output handling */
    if (inbuf && !shell_sigint) {
        if (job->out_file) {
            /* ensure file exists (create if needed) */
            init_ramfs_create(job->out_file); /* ignore error if exists */
            /* write to file via init API */
            init_ramfs_write(job->out_file, inbuf, in_len, job->append);
        } else {
            /* print to console or PTY */
            size_t off = 0;
            while (off < in_len) {
                size_t to = in_len - off;
                if (to > 128) to = 128;
                char tbuf[129];
                memcpy(tbuf, inbuf + off, to);
                tbuf[to] = '\0';
                
                if (job->pty) {
                    for (int k = 0; k < (int)to; ++k) pty_write_out(job->pty, tbuf[k]);
                } else {
                    init_puts(tbuf);
                }
                off += to;
            }
        }
    }

    /* Cleanup any remaining buffers (usually just the last one, or all if we broke early) */
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
            if (tcount < 64) tokens[tcount++] = tok;
            else kfree(tok); /* discard token if too many */
            if (i < L && buf[i] == q) ++i; /* skip closing quote */
        } else if (c == '|' || c == '&' || c == '>') {
            if (c == '>' && i + 1 < L && buf[i+1] == '>') {
                char *tok = kmalloc(3); tok[0] = '>'; tok[1] = '>'; tok[2] = '\0'; 
                if (tcount < 64) tokens[tcount++] = tok; else kfree(tok);
                i += 2;
            } else {
                char *tok = kmalloc(2); tok[0] = c; tok[1] = '\0'; 
                if (tcount < 64) tokens[tcount++] = tok; else kfree(tok);
                ++i;
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
            if (tcount < 64) tokens[tcount++] = tok; else kfree(tok);
        }
    }

    struct pipeline_job *job = kmalloc(sizeof(*job));
    if (!job) {
        for (int k = 0; k < tcount; ++k) kfree(tokens[k]);
        kfree(buf);
        return NULL;
    }
    memset(job, 0, sizeof(*job));
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

static int cmd_exit(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)argc; (void)argv; (void)in; (void)in_len; (void)out; (void)out_cap;
    shell_should_exit = 1;
    return 0;
}



/* Builtins removed. See kernel programs. */

/* shell main */
void shell_main(void *arg) {
    struct pty *_pty = (struct pty *)arg;
    
    if (_pty) {
        // *(volatile int*)0 = 0; // Trigger Data Abort
        // uart_puts("[shell] PROBE SURVIVED (This should not happen)\n");
    }
    shell_should_exit = 0;
    
    uart_puts("[shell] starting with arg="); uart_put_hex((uintptr_t)arg); uart_puts("\n");

    if (_pty) {
        /* Write banner to PTY */
        const char *b = "myras shell v0.2 (PTY)\nType 'help' for commands.\n";
        for (const char *s = b; *s; ++s) pty_write_out(_pty, *s);
    } else {
        shell_puts("myras shell v0.2\nType 'help' for commands.\n");
    }

    /* Use larger buffer for input line */
    #define LINE_BUF_SIZE 2048
    char *line = kmalloc(LINE_BUF_SIZE);
    if (!line) {
        return; 
    }
    for (;;) {
        if (shell_should_exit) break;
        
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
        } else {
            strcpy(pbuf, "myras> ");
        }

        if (_pty) {
            for (char *s = pbuf; *s; ++s) pty_write_out(_pty, *s);
        } else {
            shell_puts(pbuf);
        }

        int len = 0;
        if (_pty) {
            /* Blocking line read handled by PTY driver */
            len = pty_getline(_pty, line, LINE_BUF_SIZE);
        } else {
            len = shell_read_line(line, LINE_BUF_SIZE);
        }

        if (len <= 0) { yield(); continue; }

        struct pipeline_job *job = parse_pipeline(line);
        if (!job) { 
            if (_pty) { const char *e="error parsing\n"; for(const char*s=e;*s;++s) pty_write_out(_pty,*s); }
            else shell_puts("error parsing\n"); 
            continue; 
        }
        /* Pass PTY to job */
        job->pty = _pty;

        if (job->background) {
            /* create a background wrapper task that runs once and then disables itself */
            int pid = task_create(background_wrapper, job, "background");
            /* print background pid */
            char bbuf[32]; int bl = 0;
            int tmp = pid; if (tmp==0) { bbuf[bl++]='0'; }
            else { int p10[16]; int pi=0; while (tmp>0 && pi<16) { p10[pi++]=tmp%10; tmp/=10; } for (int j=pi-1;j>=0;--j) bbuf[bl++]= '0' + p10[j]; }
            bbuf[bl++]='\n'; bbuf[bl]=0; 
            if (_pty) {
                const char *msg = "started pid ";
                for(const char*s=msg;*s;++s) pty_write_out(_pty,*s);
                for(char*s=bbuf;*s;++s) pty_write_out(_pty,*s);
            } else {
                init_puts("started pid "); init_puts(bbuf);
            }
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
    /* Exiting shell */
    int pid = task_current_id();
    if (pid > 0) task_set_fn_null(pid);
}

int shell_exec(const char *cmdline, char *out, size_t out_cap) {
    if (!cmdline || !out || out_cap == 0) return -1;
    /* uart_puts("[shell_exec] running: "); uart_puts(cmdline); uart_puts("\n"); */
    struct pipeline_job *job = parse_pipeline(cmdline);
    if (!job) {
        uart_puts("[shell_exec] PARSE FAILED\n");
        return -1;
    }
    
    // Clear any potential PTY since we are capturing
    job->pty = NULL;
    
    // Run the pipeline
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
        if (i > 0) bufs[i-1] = NULL;

        inbuf = bufs[i];
        in_len = (wrote > 0) ? (size_t)wrote : 0;
        if (shell_sigint) break;
        yield();
    }

    int total_output = 0;
    if (inbuf && !shell_sigint) {
        // Here's the capture: Copy inbuf to 'out' instead of printing
        total_output = (int)in_len;
        if (total_output > (int)out_cap) total_output = (int)out_cap;
        memcpy(out, inbuf, total_output);
        if (total_output < (int)out_cap) out[total_output] = '\0';
    }

    // Cleanup job
    for (int i = 0; i < job->ncmds; ++i) if (bufs[i]) kfree(bufs[i]);
    for (int i = 0; i < job->ncmds; ++i) if (job->argvs[i]) kfree(job->argvs[i]);
    for (int t = 0; t < job->token_count; ++t) if (job->tokens[t]) kfree(job->tokens[t]);
    if (job->tokens) kfree(job->tokens);
    if (job->out_file) kfree(job->out_file);
    kfree(job);

    return total_output;
}
