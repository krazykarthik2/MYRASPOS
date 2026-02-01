#include "syscall.h"
#include "uart.h"
#include "ramfs.h"
#include "service.h"
#include "timer.h"
#include "framebuffer.h"
#include <stdint.h>
#include "pty.h"
#include "sched.h"

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
    // uart_puts("Syscall "); uart_put_hex(num); uart_puts("\n");
    return table[num](a0, a1, a2);
}

// Small helper syscall implementations (internal)
static uintptr_t sys_puts(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    const char *s = (const char *)a0;
    
    struct pty *p = (struct pty *)task_get_tty(task_current_id());
    if (p) {
        while (*s) pty_write_out(p, *s++);
    } else {
        uart_puts(s);
        // if (fb_is_init()) fb_puts(s);
    }
    return 0;
}

static uintptr_t sys_getc(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a0; (void)a1; (void)a2;
    struct pty *p = (struct pty *)task_get_tty(task_current_id());
    if (p) return (uintptr_t)pty_read_in(p);
    return (uintptr_t)uart_getc();
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
static uintptr_t sys_ramfs_export(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    const char *path = (const char *)a0;
    return (uintptr_t)ramfs_export(path);
}
static uintptr_t sys_ramfs_import(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    const char *path = (const char *)a0;
    return (uintptr_t)ramfs_import(path);
}
static uintptr_t sys_ramfs_remove_recursive(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    const char *path = (const char *)a0;
    return (uintptr_t)ramfs_remove_recursive(path);
}

/* service syscalls */
static uintptr_t sys_service_load_all(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a0; (void)a1; (void)a2;
    return (uintptr_t)services_load_all();
}
static uintptr_t sys_service_load_unit(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    const char *path = (const char *)a0;
    return (uintptr_t)service_load_unit(path);
}
static uintptr_t sys_service_start(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    const char *name = (const char *)a0;
    return (uintptr_t)service_start(name);
}
static uintptr_t sys_service_stop(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    const char *name = (const char *)a0;
    return (uintptr_t)service_stop(name);
}
static uintptr_t sys_service_restart(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    const char *name = (const char *)a0;
    return (uintptr_t)service_restart(name);
}
static uintptr_t sys_service_reload(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    const char *name = (const char *)a0;
    return (uintptr_t)service_reload(name);
}
static uintptr_t sys_service_enable(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    const char *name = (const char *)a0;
    return (uintptr_t)service_enable(name);
}
static uintptr_t sys_service_disable(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    const char *name = (const char *)a0;
    return (uintptr_t)service_disable(name);
}
static uintptr_t sys_service_status(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    const char *name = (const char *)a0;
    char *buf = (char *)a1; size_t len = (size_t)a2;
    return (uintptr_t)service_status(name, buf, len);
}
/* timing syscalls */
static uintptr_t sys_time(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a0; (void)a1; (void)a2;
    return (uintptr_t)timer_get_ms();
}
static uintptr_t sys_sleep(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a1; (void)a2;
    uint32_t ms = (uint32_t)a0;
    uart_puts("sys_sleep: calling sleep_ms\n");
    timer_sleep_ms(ms);
    uart_puts("sys_sleep: returned\n");
    return 0;
}
static uintptr_t sys_yield(uintptr_t a0, uintptr_t a1, uintptr_t a2) {
    (void)a0; (void)a1; (void)a2;
    schedule();
    return 0;
}

// Optionally register default syscalls during init from outside

void syscall_register_defaults(void) {
    syscall_register(SYS_PUTS, sys_puts);
    syscall_register(SYS_YIELD, sys_yield); /* Moved to 12 to avoid collision with RAMFS_CREATE (2) */
    syscall_register(SYS_GETC, sys_getc);
    syscall_register(SYS_RAMFS_CREATE, sys_ramfs_create);
    syscall_register(SYS_RAMFS_WRITE, sys_ramfs_write);
    syscall_register(SYS_RAMFS_READ, sys_ramfs_read); // Register read syscall with number 4
    syscall_register(SYS_RAMFS_REMOVE, sys_ramfs_remove);
    syscall_register(SYS_RAMFS_MKDIR, sys_ramfs_mkdir);
    syscall_register(SYS_RAMFS_LIST, sys_ramfs_list);
    syscall_register(SYS_RAMFS_EXPORT, sys_ramfs_export);
    syscall_register(SYS_RAMFS_IMPORT, sys_ramfs_import);
    syscall_register(SYS_RAMFS_REMOVE_RECURSIVE, sys_ramfs_remove_recursive);
    /* service syscalls */
    syscall_register(SYS_SERVICE_LOAD_ALL, sys_service_load_all);
    syscall_register(SYS_SERVICE_LOAD_UNIT, sys_service_load_unit);
    syscall_register(SYS_SERVICE_START, sys_service_start);
    syscall_register(SYS_SERVICE_STOP, sys_service_stop);
    syscall_register(SYS_SERVICE_RESTART, sys_service_restart);
    syscall_register(SYS_SERVICE_RELOAD, sys_service_reload);
    syscall_register(SYS_SERVICE_ENABLE, sys_service_enable);
    syscall_register(SYS_SERVICE_DISABLE, sys_service_disable);
    syscall_register(SYS_SERVICE_STATUS, sys_service_status);
    /* timing */
    syscall_register(SYS_TIME, sys_time);
    syscall_register(SYS_SLEEP, sys_sleep);
}
