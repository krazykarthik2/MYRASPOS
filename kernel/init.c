#include "init.h"
#include "syscall.h"
#include "sched.h"
#include "kmalloc.h"
#include "lib.h"
#include <stddef.h>
#include <stdint.h>
#include "wm.h"
#include "virtio.h"
#include "uart.h"
// #include "apps/terminal_app.h"

/* Exported helpers for shell to call which perform syscalls on behalf of shell */
void init_puts(const char *s) {
    syscall_handle(SYS_PUTS, (uintptr_t)s, 0, 0);
}

char init_getc(void) {
    return (char)syscall_handle(SYS_GETC, 0, 0, 0);
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
    task_create(shell_main, NULL, "shell");
}

void init_main(void *arg) {
    (void)arg;
    static int initialized = 0;
    if (initialized) {
        for (;;) yield();
    }
    initialized = 1;
    init_puts("[init] starting services...\n");

    /* create some default service units */
    init_ramfs_mkdir("/etc/");
    init_ramfs_mkdir("/var/");
    init_ramfs_mkdir("/etc/systemd/system/");
    
    /* info.service: reports system info to a log file periodically */
    const char *info_unit = 
        "[Unit]\n"
        "Description=System Information Service\n"
        "\n"
        "[Service]\n"
        "ExecStart=help > /var/log/system.info\n";
    init_ramfs_create("/etc/systemd/system/info.service");
    init_ramfs_write("/etc/systemd/system/info.service", info_unit, strlen(info_unit), 0);

    /* boot.service: simple marker */
    const char *boot_unit =
        "[Unit]\n"
        "Description=Boot Logger\n"
        "\n"
        "[Service]\n"
        "ExecStart=echo Service System Started > /var/log/boot.log\n";
    init_ramfs_create("/etc/systemd/system/boot.service");
    init_ramfs_write("/etc/systemd/system/boot.service", boot_unit, strlen(boot_unit), 0);

    /* load and start */
    init_service_load_all();
    init_service_start("boot");
    init_service_start("info");

    /* Start GUI and Input system */
    /* Start GUI and Input system */
    // Always try to start GUI for now, even if input fails
    virtio_input_init();
    
    // Initialize VFS
    extern void files_init(void);
    files_init();
    
    // Initialize DiskFS and load assets
    uart_puts("[init] DEBUG: about to init diskfs...\n");
    extern void diskfs_init(void);
    extern void diskfs_sync_to_ramfs(void);
    diskfs_init();
    diskfs_sync_to_ramfs();

    init_puts("[init] GUI subsystem starting...\n");
    wm_init();
    wm_start_task();
    /* Terminal is no longer auto-started - user can launch from Myra */

    init_puts("[init] starting shell...\n");
    init_start_shell();
    
    /* Sync files created during init to diskfs */
    extern void diskfs_sync_from_ramfs(void);
    diskfs_sync_from_ramfs();

    for (;;) {
        yield();
    }
}
