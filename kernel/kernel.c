#include "kernel.h"
#include "uart.h"
#include "kmalloc.h"
#include "palloc.h"
#include "sched.h"
#include "syscall.h"
#include "ramfs.h"
#include "timer.h"
#include "irq.h"
#include "framebuffer.h"
#include "virtio.h"
#include "service.h"

/* Larger page pool for palloc (4MB) */
static unsigned char palloc_pool[PAGE_SIZE * 1024] __attribute__((aligned(PAGE_SIZE)));

/* forward declaration of init task in init.c */
extern void init_main(void *arg);

void kernel_main(void) {
    /* basic subsystem init */
    palloc_init(palloc_pool, 1024);
    kmalloc_init();
    ramfs_init();
    services_init();
    // uart_puts("[kernel] booting...\n");

    /* Try to initialize virtio-gpu (preferred). If that fails, fall back
       to the fixed RAMFB address (for QEMU variants that expose it). */
    if (virtio_gpu_init() == 0) {
        int w = virtio_gpu_get_width();
        int h = virtio_gpu_get_height();
        uart_puts("[kernel] virtio-gpu initialized (");
        uart_put_hex(w); uart_puts("x"); uart_put_hex(h); uart_puts(")\n");
        fb_init((void *)0x42000000, w, h, w*4);
    } else {
        uart_puts("[kernel] virtio-gpu not available; falling back to RAMFB at 0x42000000\n");
        /* Quick probe... */
        volatile uint32_t *probe = (volatile uint32_t *)0x42000000;
        unsigned int sum = 0;
        for (int i = 0; i < 1024; ++i) {
            probe[i] = (uint32_t)(0xA5A50000 | (unsigned int)i);
        }
        for (int i = 0; i < 1024; ++i) sum += (unsigned int)probe[i];

        /* format hex into small buffer */
        char buf[11]; /* 0x + 8 hex + \0 */
        unsigned int v = sum;
        buf[0] = '0'; buf[1] = 'x';
        for (int i = 0; i < 8; ++i) {
            int shift = (7 - i) * 4;
            unsigned int nib = (v >> shift) & 0xF;
            buf[2 + i] = (nib < 10) ? ('0' + nib) : ('A' + (nib - 10));
        }
        buf[10] = '\0';
        uart_puts("[kernel] ramfb probe checksum= "); uart_puts(buf); uart_puts("\n");

        fb_init((void *)0x42000000, 800, 600, 800*4);
    }

    if (fb_is_init()) {
        fb_fill(0x000000);
        fb_put_text_centered("HELLO FROM MYRAS", 0xFFFFFFFF);
    }

    /* scheduler */
    /* timers must be initialized before scheduling/preemption features */
    timer_init();
    irq_init();
    scheduler_init();

    /* syscalls */
    syscall_init();
    syscall_register_defaults();

    /* create init task which will run a shell - needs large stack for bootup (virtio, services, etc.) */
    task_create_with_stack(init_main, NULL, "init", 64);

    /* run scheduler loop cooperatively */
    while (1) {
        schedule();
    }
}