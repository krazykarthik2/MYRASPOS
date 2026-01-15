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

int ramfs_remove(const char *name) {
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
    return -1;
}
