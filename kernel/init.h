#ifndef INIT_H
#define INIT_H

#include <stddef.h>

void init_puts(const char *s);
char init_getc(void);
int init_ramfs_create(const char *name);
int init_ramfs_write(const char *name, const void *buf, size_t len, int append);
int init_ramfs_read(const char *name, void *buf, size_t len);
int init_ramfs_remove(const char *name);
int init_ramfs_mkdir(const char *name);
int init_ramfs_list(const char *dir, char *buf, size_t len);
int init_ramfs_remove_recursive(const char *path);

/* resolve a possibly-relative path to an allocated absolute path using the
	shell's current working directory. Caller must free with kfree(). */
char *init_resolve_path(const char *p);

/* service manager helpers (syscall wrappers) */
int init_service_load_all(void);
int init_service_load_unit(const char *path);
int init_service_start(const char *name);
int init_service_stop(const char *name);
int init_service_restart(const char *name);
int init_service_reload(const char *name);
int init_service_enable(const char *name);
int init_service_disable(const char *name);
int init_service_status(const char *name, char *buf, size_t len);
int init_ramfs_export(const char *path);
int init_ramfs_import(const char *path);

/* start the shell task from init */
void init_start_shell(void);

#endif
