#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

typedef uintptr_t (*syscall_fn)(uintptr_t, uintptr_t, uintptr_t);

void syscall_init(void);
int syscall_register(uint32_t num, syscall_fn fn);
uintptr_t syscall_handle(uint32_t num, uintptr_t a0, uintptr_t a1, uintptr_t a2);

/* common syscall numbers */
#define SYS_PUTS 1
#define SYS_GETC 11
#define SYS_RAMFS_CREATE 2
#define SYS_RAMFS_WRITE 3
/* extra ramfs helpers */
#define SYS_RAMFS_READ 4
#define SYS_RAMFS_REMOVE 5
/* more ramfs */
#define SYS_RAMFS_MKDIR 6
#define SYS_RAMFS_LIST 7
/* ramfs export/import */
#define SYS_RAMFS_EXPORT 8
#define SYS_RAMFS_IMPORT 9
/* recursive remove */
#define SYS_RAMFS_REMOVE_RECURSIVE 10

/* service manager syscalls */
#define SYS_SERVICE_LOAD_ALL 16    /* scan /etc/systemd/system and load units */
#define SYS_SERVICE_LOAD_UNIT 17   /* load a single unit: a0 = const char *path */
#define SYS_SERVICE_START 18       /* a0 = const char *name */
#define SYS_SERVICE_STOP 19        /* a0 = const char *name */
#define SYS_SERVICE_RESTART 20     /* a0 = const char *name */
#define SYS_SERVICE_RELOAD 21      /* reload units (a0 = NULL => reload all, or a0 = name) */
#define SYS_SERVICE_ENABLE 22      /* a0 = const char *name */
#define SYS_SERVICE_DISABLE 23     /* a0 = const char *name */
#define SYS_SERVICE_STATUS 24      /* a0 = const char *name, a1 = char *buf, a2 = size_t len */

/* timing syscalls */
#define SYS_TIME 30   /* returns monotonic ms */
#define SYS_SLEEP 31  /* a0 = ms to sleep */

/* register helper/default syscalls */
void syscall_register_defaults(void);

#endif
