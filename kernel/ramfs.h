#ifndef RAMFS_H
#define RAMFS_H

#include <stddef.h>

int ramfs_init(void);
int ramfs_create(const char *name);
int ramfs_write(const char *name, const void *buf, size_t len, size_t offset);
int ramfs_read(const char *name, void *buf, size_t len, size_t offset);
int ramfs_remove(const char *name);
int ramfs_remove_recursive(const char *name);
int ramfs_mkdir(const char *name);
int ramfs_list(const char *dir, char *buf, size_t len);
int ramfs_is_dir(const char *name);
/* serialize entire ramfs into a single file at `path` (within ramfs)
	format: repeated entries: [u32 name_len][name bytes][u32 data_len][data bytes]
	terminated by name_len == 0
*/
int ramfs_export(const char *path);
int ramfs_import(const char *path);
int ramfs_get_size(const char *name);

#endif
