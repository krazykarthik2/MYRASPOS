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

int ramfs_init(void) {
    root = NULL;
    return 0;
}

static struct ram_node *find_node(const char *name) {
    for (struct ram_node *n = root; n; n = n->next) {
        if (strncmp(n->name, name, RAMFS_NAME_MAX) == 0) return n;
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
        // avoid duplicates by checking existing buf
        int dup = 0;
        size_t p = 0;
        while (p < off) {
            size_t l = strlen(&buf[p]);
            if (strcmp(&buf[p], ent) == 0) { dup = 1; break; }
            p += l + 1;
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
