#include "kernel.h"
#include "uart.h"
#include "rpi_fx.h"
#include "framebuffer.h"
#include "palloc.h"
#include "kmalloc.h"
#include "mmu.h"
#include "irq.h"
#include "timer.h"
#include "virtio.h"
#include "sched.h"
#include "ramfs.h"
#include "init.h"
#include "syscall.h"
#include <stdint.h>


int screen_w = 1024, screen_h = 768; // to be externed to sched.c

void heartbeat_task(void *arg);

void heartbeat_task(void *arg) {
    (void)arg;
#ifdef DEBUG
    uart_puts("[heartbeat] task started\n");
#endif
    while (1) {
#ifdef REAL
        rpi_gpio16_on();
#endif
        task_block_current_until(scheduler_get_tick() + 500);
        task_block_current_until(scheduler_get_tick() + 500);
#ifdef REAL
        rpi_gpio16_off();
#endif
        task_block_current_until(scheduler_get_tick() + 500);
    }
}

extern char __end[];

void kernel_main(void) {
    /* 1. Initialize hardware basics */
    uart_init();     
#ifdef DEBUG
    uart_puts("\n[kernel] UART START\n");
    uart_puts("[kernel] alive\n");
#endif

#ifdef REAL
    rpi_init();
    if (rpi_gpu_init() == 0) {
        fb_fill(0xFF0000FF); // Start with RED
        fb_put_text_centered("MYRAS OS BOOTING (RPI)...", 0xFFFFFFFF);
        rpi_gpu_flush();
    }
    extern int usb_init(void);
    usb_init();
#else
    virtio_init();      
    if (virtio_gpu_init() == 0) {
        fb_fill(0xFF0000FF); // Start with RED
        fb_put_text_centered("MYRAS OS BOOTING (VIRTIO)...", 0xFFFFFFFF);
        virtio_gpu_flush();
    }
#endif
    else {
#ifdef DEBUG
        uart_puts("[kernel] GPU Init Failed!\n");
#endif
    }

    /* 3. Kernel Subsystems */
#ifdef DEBUG
    uart_puts("[kernel] palloc_init... ");
#endif
    uintptr_t pool_start;
    size_t pool_pages;
#ifdef REAL
    pool_start = ((uintptr_t)__end + 4095) & ~4095ULL;
    /* Pi Zero 2 W has 512MB RAM. Upper bound safe at 400MB to leave room for BSS/Stack/GPU */
    pool_pages = (0x19000000 - pool_start) / 4096;
#else
    pool_start = ((uintptr_t)__end + 4095) & ~4095ULL;
    /* QEMU virt has RAM from 0x40000000 to up to 0x60000000 depending on -m */
    pool_pages = (0x60000000 - pool_start) / 4096;
#endif
    palloc_init((void*)pool_start, pool_pages);
    fb_fill(0xFFFF0000); // Progress: BLUE
    fb_put_text_centered("PALLOC DONE", 0xFFFFFFFF);
    virtio_gpu_flush();
#ifdef DEBUG
    uart_puts("done.\n");
    
    uart_puts("[kernel] kmalloc_init... ");
#endif
    kmalloc_init();
    fb_fill(0xFF00FF00); // Progress: GREEN
    fb_put_text_centered("KMALLOC DONE", 0xFFFFFFFF);
    virtio_gpu_flush();
    uart_puts("done.\n");
    
    uart_puts("[kernel] mmu_init... ");
    mmu_init();
    // If we survive this, we change color again
    fb_fill(0xFFFFFF00); // Progress: YELLOW
    fb_put_text_centered("MMU ENABLED (IDENTITY)", 0xFF000000);
    virtio_gpu_flush();
#ifdef DEBUG
    uart_puts("done.\n");
    
    uart_puts("[kernel] irq_init... ");
#endif
    irq_init();
#ifdef DEBUG
    uart_puts("done.\n");
#endif
    
    uart_puts("[kernel] timer_init... ");
    timer_init();
    fb_fill(0xFFFF00FF); // Progress: MAGENTA
    fb_put_text_centered("TIMER ENABLED", 0xFFFFFFFF);
    virtio_gpu_flush();
#ifdef DEBUG
    uart_puts("done.\n");
    
    uart_puts("[kernel] scheduler_init... ");
#endif
    scheduler_init();
#ifdef DEBUG
    uart_puts("done.\n");
#endif

    /* Initialize Syscalls */
#ifdef DEBUG
    uart_puts("[kernel] syscall_init... ");
#endif
    syscall_init();
    syscall_register_defaults();
#ifdef DEBUG
    uart_puts("done.\n");
#endif

    /* 4. Services and Tasks */
    ramfs_init();
    task_create(heartbeat_task, NULL, "heartbeat");
    task_create(init_main, NULL, "init");

    fb_fill(0x00000000); // BLACK - Launching system
    
    fb_get_res(&screen_w, &screen_h);
    
    fb_put_text_centered("LAUNCHING MULTITASKING...", 0xFFFFFFFF);
    virtio_gpu_flush();
#ifdef DEBUG
    uart_puts("[kernel] entering scheduler loop...\n");
#endif
    
    fb_fill(0xFF00FF00); // GREEN
    fb_put_text_centered("LAUNCHING MULTITASKING...", 0xFF000000);
    virtio_gpu_flush();
    
    int loop_cnt = 0;
    while (1) {
        schedule();
        
        // Visual heartbeat every ~10000 loops
        if (++loop_cnt > 100000) {
            static int toggle = 0;
            int h_x = screen_w/2 - 10;
            int h_y = screen_h/2 - 10;
            // Draw a small square in center
            fb_draw_rect(h_x, h_y, 20, 20, toggle ? 0xFFFFFFFF : 0xFF000000);
            virtio_gpu_flush_rect(h_x, h_y, 20, 20); // Optimized flush
            toggle = !toggle;
            loop_cnt = 0;
        }
    }
}
