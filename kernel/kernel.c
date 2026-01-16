#include "kernel.h"
#include "uart.h"
#include "kmalloc.h"
#include "palloc.h"
#include "sched.h"
#include "syscall.h"
#include "ramfs.h"

/* Small static page pool for palloc */
static unsigned char palloc_pool[PAGE_SIZE * 16];

/* forward declaration of init task in init.c */
extern void init_main(void *arg);

void kernel_main(void) {
    /* basic subsystem init */
    palloc_init(palloc_pool, 16);
    kmalloc_init();
    ramfs_init();

    /* scheduler */
    scheduler_init();

    /* syscalls */
    syscall_init();
    syscall_register_defaults();

    /* create init task which will run a shell */
    task_create(init_main, NULL);

    /* run scheduler loop cooperatively */
    while (1) {
        schedule();
    }
}