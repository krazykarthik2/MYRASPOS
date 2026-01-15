#ifndef RAMFS_H
#define RAMFS_H

#include <stddef.h>

int ramfs_init(void);
int ramfs_create(const char *name);
int ramfs_write(const char *name, const void *buf, size_t len, size_t offset);
int ramfs_read(const char *name, void *buf, size_t len, size_t offset);
int ramfs_remove(const char *name);

#endif
