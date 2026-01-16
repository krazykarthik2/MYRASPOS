#ifndef INIT_H
#define INIT_H

#include <stddef.h>

void init_puts(const char *s);
int init_ramfs_create(const char *name);
int init_ramfs_write(const char *name, const void *buf, size_t len, int append);
int init_ramfs_read(const char *name, void *buf, size_t len);
int init_ramfs_remove(const char *name);
int init_ramfs_mkdir(const char *name);
int init_ramfs_list(const char *dir, char *buf, size_t len);

/* start the shell task from init */
void init_start_shell(void);

#endif
