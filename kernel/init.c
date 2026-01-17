#include "init.h"
#include "syscall.h"
#include "sched.h"
#include "kmalloc.h"
#include "lib.h"
#include <stddef.h>
#include <stdint.h>

/* Exported helpers for shell to call which perform syscalls on behalf of shell */
void init_puts(const char *s) {
    syscall_handle(SYS_PUTS, (uintptr_t)s, 0, 0);
}

int init_ramfs_create(const char *name) {
    return (int)syscall_handle(SYS_RAMFS_CREATE, (uintptr_t)name, 0, 0);
}

int init_ramfs_read(const char *name, void *buf, size_t len) {
    return (int)syscall_handle(SYS_RAMFS_READ, (uintptr_t)name, (uintptr_t)buf, (uintptr_t)len);
}

int init_ramfs_remove(const char *name) {
    return (int)syscall_handle(SYS_RAMFS_REMOVE, (uintptr_t)name, 0, 0);
}

int init_ramfs_mkdir(const char *name) {
    return (int)syscall_handle(SYS_RAMFS_MKDIR, (uintptr_t)name, 0, 0);
}

int init_ramfs_list(const char *dir, char *buf, size_t len) {
    return (int)syscall_handle(SYS_RAMFS_LIST, (uintptr_t)dir, (uintptr_t)buf, (uintptr_t)len);
}

int init_ramfs_export(const char *path) {
    return (int)syscall_handle(SYS_RAMFS_EXPORT, (uintptr_t)path, 0, 0);
}

int init_ramfs_import(const char *path) {
    return (int)syscall_handle(SYS_RAMFS_IMPORT, (uintptr_t)path, 0, 0);
}

int init_ramfs_remove_recursive(const char *path) {
    return (int)syscall_handle(SYS_RAMFS_REMOVE_RECURSIVE, (uintptr_t)path, 0, 0);
}

int init_service_load_all(void) {
    return (int)syscall_handle(SYS_SERVICE_LOAD_ALL, 0, 0, 0);
}

int init_service_load_unit(const char *path) {
    return (int)syscall_handle(SYS_SERVICE_LOAD_UNIT, (uintptr_t)path, 0, 0);
}

int init_service_start(const char *name) {
    return (int)syscall_handle(SYS_SERVICE_START, (uintptr_t)name, 0, 0);
}

int init_service_stop(const char *name) {
    return (int)syscall_handle(SYS_SERVICE_STOP, (uintptr_t)name, 0, 0);
}

int init_service_restart(const char *name) {
    return (int)syscall_handle(SYS_SERVICE_RESTART, (uintptr_t)name, 0, 0);
}

int init_service_reload(const char *name) {
    return (int)syscall_handle(SYS_SERVICE_RELOAD, (uintptr_t)name, 0, 0);
}

int init_service_enable(const char *name) {
    return (int)syscall_handle(SYS_SERVICE_ENABLE, (uintptr_t)name, 0, 0);
}

int init_service_disable(const char *name) {
    return (int)syscall_handle(SYS_SERVICE_DISABLE, (uintptr_t)name, 0, 0);
}

int init_service_status(const char *name, char *buf, size_t len) {
    return (int)syscall_handle(SYS_SERVICE_STATUS, (uintptr_t)name, (uintptr_t)buf, (uintptr_t)len);
}

int init_ramfs_write(const char *name, const void *buf, size_t len, int append) {
    if (!append) {
        return (int)syscall_handle(SYS_RAMFS_WRITE, (uintptr_t)name, (uintptr_t)buf, (uintptr_t)len);
    }
    /* Append: read existing, concatenate, recreate file and write back */
    size_t max_read = 4096;
    char *tmp = kmalloc(max_read);
    if (!tmp) return -1;
    int r = init_ramfs_read(name, tmp, max_read);
    size_t existing = 0;
    if (r > 0) existing = (size_t)r;
    char *combined = kmalloc(existing + len);
    if (!combined) { kfree(tmp); return -1; }
    if (existing) memcpy(combined, tmp, existing);
    memcpy(combined + existing, buf, len);
    kfree(tmp);
    init_ramfs_remove(name);
    init_ramfs_create(name);
    int w = (int)syscall_handle(SYS_RAMFS_WRITE, (uintptr_t)name, (uintptr_t)combined, (uintptr_t)(existing + len));
    kfree(combined);
    return w;
}

/* Start the shell task (shell_main implemented in shell.c) */
extern void shell_main(void *arg);
void init_start_shell(void) {
    task_create(shell_main, NULL);
}

void init_main(void *arg) {
    (void)arg;
    init_puts("[init] starting shell...\n");
    init_start_shell();
    for (;;) {
        yield();
    }
}
