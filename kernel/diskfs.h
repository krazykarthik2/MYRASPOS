#ifndef DISKFS_H
#define DISKFS_H

#include <stddef.h>
#include <stdint.h>

void diskfs_init(void);
int diskfs_create(const char *name);
int diskfs_write(const char *name, const void *buf, size_t len, size_t offset);
int diskfs_read(const char *name, void *buf, size_t len, size_t offset);
int diskfs_list(const char *dir, char *buf, size_t len);

/* Move all files from ramfs to diskfs */
void diskfs_sync_from_ramfs(void);

#endif
