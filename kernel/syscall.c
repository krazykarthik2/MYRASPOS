#include "syscall.h"
#include "uart.h"
#include "ramfs.h"
#include <stddef.h>
#include <stdint.h>

#define SYSCALL_MAX 64

static syscall_fn table[SYSCALL_MAX];

void syscall_init(void) {
    for (int i = 0; i < SYSCALL_MAX; ++i) table[i] = NULL;
}

int syscall_register(uint32_t num, syscall_fn fn) {
    if (num >= SYSCALL_MAX) return -1;
    table[num] = fn;
    return 0;
}

uintptr_t syscall_handle(uint32_t num, uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    if (num >= SYSCALL_MAX || !table[num]) return (uintptr_t)-1;
    return table[num](a0, a1, a2);
}

// Small helper syscall implementations (internal)
static uintptr_t sys_puts(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    const char *s = (const char *)a0;
    uart_puts(s);
    return 0;
}

static uintptr_t sys_ramfs_create(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    return (uintptr_t)ramfs_create((const char *)a0);
}

static uintptr_t sys_ramfs_write(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    const char *name = (const char *)a0;
    const void *buf = (const void *)a1;
    size_t len = (size_t)a2;
    return (uintptr_t)ramfs_write(name, buf, len, 0);
}
static uintptr_t sys_ramfs_read(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    const char *name = (const char *)a0;
    void *buf = (void *)a1;
    size_t len = (size_t)a2;
    return (uintptr_t)ramfs_read(name, buf, len, 0);
}
static uintptr_t sys_ramfs_remove(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    const char *name = (const char *)a0;
    return (uintptr_t)ramfs_remove(name);
}
static uintptr_t sys_ramfs_mkdir(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    const char *name = (const char *)a0;
    return (uintptr_t)ramfs_mkdir(name);
}

static uintptr_t sys_ramfs_list(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    const char *dir = (const char *)a0;
    char *buf = (char *)a1;
    size_t len = (size_t)a2;
    return (uintptr_t)ramfs_list(dir, buf, len);
}
// Optionally register default syscalls during init from outside

void syscall_register_defaults(void) {
    syscall_register(SYS_PUTS, sys_puts);
    syscall_register(SYS_RAMFS_CREATE, sys_ramfs_create);
    syscall_register(SYS_RAMFS_WRITE, sys_ramfs_write);
    syscall_register(SYS_RAMFS_READ, sys_ramfs_read); // Register read syscall with number 4
    syscall_register(SYS_RAMFS_REMOVE, sys_ramfs_remove);
    syscall_register(SYS_RAMFS_MKDIR, sys_ramfs_mkdir);
    syscall_register(SYS_RAMFS_LIST, sys_ramfs_list);
}
