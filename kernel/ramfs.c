#include "ramfs.h"
#include "kmalloc.h"
#include <string.h>
#include <stdint.h>

#define RAMFS_NAME_MAX 64

struct ram_node {
    char name[RAMFS_NAME_MAX];
    size_t size;
    uint8_t *data;
    struct ram_node *next;
};

static struct ram_node *root = NULL;

#define PATH_CACHE_SIZE 32
struct path_cache_entry {
    char name[RAMFS_NAME_MAX];
    struct ram_node *node;
};
static struct path_cache_entry path_cache[PATH_CACHE_SIZE];
static int path_cache_next = 0;

static void invalidate_cache(void) {
    for (int i = 0; i < PATH_CACHE_SIZE; i++) path_cache[i].name[0] = '\0';
}

int ramfs_init(void) {
    root = NULL;
    return 0;
}

static struct ram_node *find_node(const char *name) {
    /* Check cache */
    for (int i = 0; i < PATH_CACHE_SIZE; i++) {
        if (path_cache[i].name[0] != '\0' && strcmp(path_cache[i].name, name) == 0) {
            return path_cache[i].node;
        }
    }

    /* Fallback to linear search */
    for (struct ram_node *n = root; n; n = n->next) {
        if (strncmp(n->name, name, RAMFS_NAME_MAX) == 0) {
            /* Update cache */
            strncpy(path_cache[path_cache_next].name, name, RAMFS_NAME_MAX - 1);
            path_cache[path_cache_next].node = n;
            path_cache_next = (path_cache_next + 1) % PATH_CACHE_SIZE;
            return n;
        }
    }
    return NULL;
}

int ramfs_create(const char *name) {
    if (find_node(name)) return -1;
    struct ram_node *n = kmalloc(sizeof(*n));
    if (!n) return -1;
    memset(n, 0, sizeof(*n));
    strncpy(n->name, name, RAMFS_NAME_MAX - 1);
    n->size = 0;
    n->data = NULL;
    n->next = root;
    root = n;
    return 0;
}

int ramfs_mkdir(const char *name) {
    // store directories with trailing slash for simplicity
    char buf[RAMFS_NAME_MAX];
    size_t l = strlen(name);
    if (l + 1 >= RAMFS_NAME_MAX) return -1;
    strcpy(buf, name);
    if (buf[l-1] != '/') {
        buf[l] = '/'; buf[l+1] = '\0';
    }
    if (find_node(buf)) return -1;
    struct ram_node *n = kmalloc(sizeof(*n));
    if (!n) return -1;
    memset(n, 0, sizeof(*n));
    strncpy(n->name, buf, RAMFS_NAME_MAX - 1);
    n->size = 0;
    n->data = NULL;
    n->next = root;
    root = n;
    return 0;
}

int ramfs_write(const char *name, const void *buf, size_t len, size_t offset) {
    struct ram_node *n = find_node(name);
    if (!n) return -1;
    size_t new_sz = offset + len;
    if (new_sz > n->size) {
        uint8_t *newdata = kmalloc(new_sz);
        if (!newdata) return -1;
        if (n->data) {
            memcpy(newdata, n->data, n->size);
            kfree(n->data);
        }
        n->data = newdata;
        n->size = new_sz;
    }
    memcpy(n->data + offset, buf, len);
    return (int)len;
}

int ramfs_read(const char *name, void *buf, size_t len, size_t offset) {
    struct ram_node *n = find_node(name);
    if (!n) return -1;
    if (offset >= n->size) return 0;
    size_t to_read = n->size - offset;
    if (to_read > len) to_read = len;
    memcpy(buf, n->data + offset, to_read);
    return (int)to_read;
}

int ramfs_list(const char *dir, char *buf, size_t len) {
    // dir should be absolute path without trailing slash except root "/"
    size_t off = 0;
    char prefix[RAMFS_NAME_MAX];
    if (strcmp(dir, "/") == 0) {
        strcpy(prefix, "/");
    } else {
        size_t l = strlen(dir);
        if (l + 1 >= RAMFS_NAME_MAX) return -1;
        strcpy(prefix, dir);
        if (prefix[l-1] != '/') { prefix[l] = '/'; prefix[l+1] = '\0'; }
    }
    // collect unique immediate children
    for (struct ram_node *n = root; n; n = n->next) {
        const char *nm = n->name;
        if (strncmp(nm, prefix, strlen(prefix)) != 0) continue;
        // skip the directory itself
        const char *rest = nm + strlen(prefix);
        if (*rest == '\0') continue;
        // find next component
        const char *slash = strchr(rest, '/');
        char ent[RAMFS_NAME_MAX];
        if (slash) {
            size_t comp_len = (size_t)(slash - rest);
            if (comp_len + 1 >= RAMFS_NAME_MAX) continue;
            memcpy(ent, rest, comp_len);
            ent[comp_len] = '/';
            ent[comp_len+1] = '\0';
        } else {
            size_t comp_len = strlen(rest);
            if (comp_len + 1 >= RAMFS_NAME_MAX) continue;
            memcpy(ent, rest, comp_len);
            ent[comp_len] = '\0';
        }
        /* avoid duplicates by checking existing entries in buf (newline-separated) */
        int dup = 0;
        size_t p = 0;
        while (p < off) {
            /* find length up to next newline */
            size_t l2 = 0;
            while (p + l2 < off && buf[p + l2] != '\n') ++l2;
            if (l2 == 0) break;
            if (l2 == strlen(ent) && memcmp(&buf[p], ent, l2) == 0) { dup = 1; break; }
            p += l2 + 1;
        }
        if (dup) continue;
        size_t entl = strlen(ent);
        if (off + entl + 1 >= len) break;
        memcpy(&buf[off], ent, entl);
        buf[off + entl] = '\n';
        off += entl + 1;
    }
    if (off < len) buf[off] = '\0';
    return (int)off;
}

int ramfs_is_dir(const char *name) {
    // treat name without trailing slash as directory if a node exists with that prefix
    char buf[RAMFS_NAME_MAX];
    size_t l = strlen(name);
    if (l + 1 >= RAMFS_NAME_MAX) return 0;
    strcpy(buf, name);
    if (buf[l-1] != '/') { buf[l] = '/'; buf[l+1] = '\0'; }
    for (struct ram_node *n = root; n; n = n->next) {
        if (strncmp(n->name, buf, strlen(buf)) == 0) return 1;
    }
    return 0;
}

int ramfs_remove(const char *name) {
    // if removing a directory, ensure empty
    struct ram_node **prev = &root;
    for (struct ram_node *n = root; n; n = n->next) {
        if (strncmp(n->name, name, RAMFS_NAME_MAX) == 0) {
            *prev = n->next;
            if (n->data) kfree(n->data);
            kfree(n);
            return 0;
        }
        prev = &n->next;
    }
    // if it's a directory name without trailing slash, remove with trailing slash
    char buf[RAMFS_NAME_MAX];
    size_t l = strlen(name);
    if (l + 1 >= RAMFS_NAME_MAX) return -1;
    strcpy(buf, name);
    if (buf[l-1] != '/') { buf[l] = '/'; buf[l+1] = '\0'; }
    // check for children
    for (struct ram_node *m = root; m; m = m->next) {
        if (strncmp(m->name, buf, strlen(buf)) == 0) {
            // if it's the same node, remove it
            if (strncmp(m->name, buf, RAMFS_NAME_MAX) == 0) {
                // remove m
                struct ram_node **pp = &root;
                for (struct ram_node *x = root; x; x = x->next) {
                    if (x == m) {
                        *pp = x->next;
                        if (x->data) kfree(x->data);
                        kfree(x);
                        return 0;
                    }
                    pp = &x->next;
                }
            }
            // contains children -> cannot remove
            return -1;
        }
    }
    return -1;
}

int ramfs_remove_recursive(const char *name) {
    if (!name) return -1;
    char prefix[RAMFS_NAME_MAX];
    size_t nlen = strlen(name);
    if (nlen + 1 >= RAMFS_NAME_MAX) return -1;
    strcpy(prefix, name);
    /* ensure prefix ends with '/' for directory matching */
    if (prefix[nlen-1] != '/') { prefix[nlen] = '/'; prefix[nlen+1] = '\0'; }
    int removed = 0;
    struct ram_node **prev = &root;
    struct ram_node *cur = root;
    while (cur) {
        struct ram_node *next = cur->next;
        /* if node name equals name exactly, or has prefix match, remove it */
        if (strncmp(cur->name, name, RAMFS_NAME_MAX) == 0 || strncmp(cur->name, prefix, strlen(prefix)) == 0) {
            *prev = next;
            if (cur->data) kfree(cur->data);
            kfree(cur);
            removed = 1;
            cur = next;
            invalidate_cache();
            continue;
        }
        prev = &cur->next;
        cur = next;
    }
    return removed ? 0 : -1;
}

/* serialize entire ramfs into single file at path (path resides in ramfs) */
int ramfs_export(const char *path) {
    /* compute needed size */
    size_t total = 0;
    for (struct ram_node *n = root; n; n = n->next) {
        size_t namelen = strlen(n->name);
        total += 4 + namelen + 4 + n->size;
    }
    total += 4; /* terminating zero name_len */
    uint8_t *buf = kmalloc(total);
    if (!buf) return -1;
    size_t off = 0;
    for (struct ram_node *n = root; n; n = n->next) {
        uint32_t namelen = (uint32_t)strlen(n->name);
        /* write namelen little-endian */
        buf[off++] = (uint8_t)(namelen & 0xff);
        buf[off++] = (uint8_t)((namelen >> 8) & 0xff);
        buf[off++] = (uint8_t)((namelen >> 16) & 0xff);
        buf[off++] = (uint8_t)((namelen >> 24) & 0xff);
        memcpy(&buf[off], n->name, namelen); off += namelen;
        uint32_t dlen = (uint32_t)n->size;
        buf[off++] = (uint8_t)(dlen & 0xff);
        buf[off++] = (uint8_t)((dlen >> 8) & 0xff);
        buf[off++] = (uint8_t)((dlen >> 16) & 0xff);
        buf[off++] = (uint8_t)((dlen >> 24) & 0xff);
        if (dlen && n->data) { memcpy(&buf[off], n->data, dlen); off += dlen; }
    }
    /* terminating zero */
    buf[off++] = 0; buf[off++] = 0; buf[off++] = 0; buf[off++] = 0;

    /* write into ramfs file */
    ramfs_remove(path);
    if (ramfs_create(path) < 0) { kfree(buf); return -1; }
    int w = ramfs_write(path, buf, off, 0);
    kfree(buf);
    return (w >= 0) ? 0 : -1;
}

int ramfs_import(const char *path) {
    /* read file */
    size_t max = 32768;
    uint8_t *buf = kmalloc(max);
    if (!buf) return -1;
    int r = ramfs_read(path, buf, max, 0);
    if (r <= 0) { kfree(buf); return -1; }
    size_t off = 0;
    while (off + 4 <= (size_t)r) {
        uint32_t namelen = (uint32_t)buf[off] | ((uint32_t)buf[off+1] << 8) | ((uint32_t)buf[off+2] << 16) | ((uint32_t)buf[off+3] << 24);
        off += 4;
        if (namelen == 0) break;
        if (off + namelen + 4 > (size_t)r) break;
        char name[RAMFS_NAME_MAX]; size_t copy_len = (namelen < RAMFS_NAME_MAX-1) ? namelen : (RAMFS_NAME_MAX-1);
        memcpy(name, &buf[off], copy_len); name[copy_len] = '\0'; off += namelen;
        uint32_t dlen = (uint32_t)buf[off] | ((uint32_t)buf[off+1] << 8) | ((uint32_t)buf[off+2] << 16) | ((uint32_t)buf[off+3] << 24);
        off += 4;
        if (off + dlen > (size_t)r) break;
        /* recreate node */
        ramfs_remove(name);
        ramfs_create(name);
        if (dlen) ramfs_write(name, &buf[off], dlen, 0);
        off += dlen;
    }
    kfree(buf);
    return 0;
}

int ramfs_get_size(const char *name) {
    struct ram_node *n = find_node(name);
    if (!n) return -1;
    return (int)n->size;
}
