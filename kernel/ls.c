#include "programs.h"
#include "init.h"
#include "lib.h"
#include "glob.h"
#include <stddef.h>

static int ls_error_notfound(const char *arg, char *out, size_t out_cap);

int prog_ls(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap) {
    (void)in; (void)in_len;
    const char *arg = NULL;
    char *resolved = NULL;
    if (argc >= 2) arg = argv[1];
    /* if no arg -> list current working directory */
    if (!arg) {
        const char *cwd = init_resolve_path(".");
        if (!cwd) return -1;
        int r = init_ramfs_list(cwd, out, out_cap);
        kfree((void*)cwd);
        if (r < 0) return (int)r; return r;
    }
    /* detect glob characters */
    int has_glob = 0;
    for (const char *p = arg; *p; ++p) { if (*p=='*' || *p=='?' || *p=='[') { has_glob = 1; break; } }
    if (has_glob) {
        /* split directory and pattern */
        const char *last = arg + strlen(arg);
        while (last > arg && *(last-1) != '/') --last;
        char dir[128]; char pat[128];
        if (last == arg) { strcpy(dir, "/"); strncpy(pat, arg, sizeof(pat)-1); pat[sizeof(pat)-1] = '\0'; }
        else {
            size_t dlen = (size_t)(last - arg);
            if (dlen >= sizeof(dir)) dlen = sizeof(dir)-1;
            memcpy(dir, arg, dlen); dir[dlen] = '\0';
            strncpy(pat, last, sizeof(pat)-1); pat[sizeof(pat)-1] = '\0';
        }
        char listbuf[1024];
        resolved = init_resolve_path(dir);
        if (!resolved) return -1;
        int r = init_ramfs_list(resolved, listbuf, sizeof(listbuf));
        kfree(resolved); resolved = NULL;
        if (r < 0) return (int)r;
        size_t off = 0; size_t p = 0;
        /* ramfs_list returns entries separated by '\n' and terminated by '\0' */
        while (p < (size_t)r) {
            /* find next newline */
            size_t l = 0;
            while (p + l < (size_t)r && listbuf[p + l] != '\n') ++l;
            if (l == 0) break;
            const char *name = &listbuf[p];
            if (glob_match(pat, name)) {
                if (off + l + 1 >= (size_t)out_cap) break;
                memcpy(out+off, name, l); off += l; out[off++] = '\n';
            }
            p += l + 1;
        }
        if (off < out_cap) out[off] = '\0';
        return (int)off;
    }
    /* no glob: if argument is '.' -> list cwd */
    if (strcmp(arg, ".") == 0) {
        char *cwd = init_resolve_path(".");
        if (!cwd) return -1;
        int r = init_ramfs_list(cwd, out, out_cap);
        kfree(cwd);
        return r >= 0 ? r : (int)r;
    }
    /* if it's a directory -> list directory (resolve relative to cwd) */
    {
        char *rp = init_resolve_path(arg);
        if (!rp) return -1;
        int r = init_ramfs_list(rp, out, out_cap);
        kfree(rp);
        if (r >= 0) return r;
    }
    /* else if exact file exists -> print basename */
    char tmp[4];
    char *rp2 = init_resolve_path(arg);
    if (!rp2) return -1;
    int exist = init_ramfs_read(rp2, tmp, sizeof(tmp));
    kfree(rp2);
    if (exist >= 0) {
        /* print basename */
        const char *last = arg + strlen(arg);
        while (last > arg && *(last-1) != '/') --last;
        size_t l = strlen(last);
        if (l + 1 > (size_t)out_cap) l = out_cap - 1;
        memcpy(out, last, l); out[l] = '\0'; return (int)l;
    }
    /* not found */
    return ls_error_notfound(arg, out, out_cap);
}

/* If path not found, return a helpful error like: ls: cannot access '<path>': No such file or directory\n */
static int ls_error_notfound(const char *arg, char *out, size_t out_cap) {
    const char *fmt1 = "ls: cannot access '\0";
    const char *fmt2 = "': No such file or directory\n";
    size_t alen = strlen(arg);
    size_t need = strlen(fmt1) - 2 + alen + strlen(fmt2) + 1;
    if (need > out_cap) {
        /* truncated message */
        if (out_cap > 0) out[0] = '\0';
        return 0;
    }
    size_t off = 0;
    memcpy(out + off, "ls: cannot access '", 19); off += 19;
    memcpy(out + off, arg, alen); off += alen;
    memcpy(out + off, "': No such file or directory\n", 31); off += 31;
    if (off < out_cap) out[off] = '\0';
    return (int)off;
}
