#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

typedef uintptr_t (*syscall_fn)(uintptr_t, uintptr_t, uintptr_t);

void syscall_init(void);
int syscall_register(uint32_t num, syscall_fn fn);
uintptr_t syscall_handle(uint32_t num, uintptr_t a0, uintptr_t a1, uintptr_t a2);

/* common syscall numbers */
#define SYS_PUTS 1
#define SYS_RAMFS_CREATE 2
#define SYS_RAMFS_WRITE 3
/* extra ramfs helpers */
#define SYS_RAMFS_READ 4
#define SYS_RAMFS_REMOVE 5
/* more ramfs */
#define SYS_RAMFS_MKDIR 6
#define SYS_RAMFS_LIST 7
#define SYS_RAMFS_READ 4

/* register helper/default syscalls */
void syscall_register_defaults(void);

#endif
