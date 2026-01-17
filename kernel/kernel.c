#include "kernel.h"
#include "uart.h"
#include "kmalloc.h"
#include "palloc.h"
#include "sched.h"
#include "syscall.h"
#include "ramfs.h"
#include "timer.h"
#include "irq.h"

/* Small static page pool for palloc (increase for more heap during testing) */
static unsigned char palloc_pool[PAGE_SIZE * 256];

/* forward declaration of init task in init.c */
extern void init_main(void *arg);

void kernel_main(void) {
    /* basic subsystem init */
    palloc_init(palloc_pool, 256);
    kmalloc_init();
    ramfs_init();

    /* scheduler */
    /* timers must be initialized before scheduling/preemption features */
    timer_init();
    irq_init();
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