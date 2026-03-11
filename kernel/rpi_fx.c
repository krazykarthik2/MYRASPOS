#include "rpi_fx.h"
#include "uart.h"
#include "framebuffer.h"
#include "debug_overlay.h"
#include <stddef.h>
#include <stdint.h>

#define MMIO_BASE 0x3F000000ULL
#define GPIO_BASE (MMIO_BASE + 0x200000)

#define GPFSEL1     (*(volatile unsigned int*)(GPIO_BASE + 0x04))
#define GPSET0      (*(volatile unsigned int*)(GPIO_BASE + 0x1C))
#define GPCLR0      (*(volatile unsigned int*)(GPIO_BASE + 0x28))

#define GPPUD       (*(volatile unsigned int*)(GPIO_BASE + 0x94))
#define GPPUDCLK0   (*(volatile unsigned int*)(GPIO_BASE + 0x98))

/* Mailbox */
#define MBOX_BASE   (MMIO_BASE + 0xB880)
#define MBOX_READ   (*(volatile unsigned int*)(MBOX_BASE + 0x00))
#define MBOX_STATUS (*(volatile unsigned int*)(MBOX_BASE + 0x18))
#define MBOX_WRITE  (*(volatile unsigned int*)(MBOX_BASE + 0x20))

#define MBOX_EMPTY  0x40000000
#define MBOX_FULL   0x80000000
#define MBOX_CH_PROP 8

volatile unsigned int __attribute__((aligned(16))) mbox[36];

void delay(unsigned int count) {
    while(count--) { asm volatile("nop"); }
}

uint32_t mailbox_clock_rate(uint32_t clock_id) {
    mbox[0] = 9*4;
    mbox[1] = 0;
    mbox[2] = 0x30002; // get clock rate tag
    mbox[3] = 12;
    mbox[4] = 8;
    mbox[5] = clock_id;
    mbox[6] = 0;
    mbox[7] = 0;
    mbox[8] = 0;
    if (mbox_call(MBOX_CH_PROP, (unsigned int *)mbox)) return mbox[6];
    return 0;
}

int mbox_call(unsigned char ch, volatile unsigned int *buffer) {
    unsigned int r = ((unsigned int)((unsigned long)buffer) & ~0xF) | (ch & 0xF);
    while (MBOX_STATUS & MBOX_FULL);
    MBOX_WRITE = r;
    while (1) {
        while (MBOX_STATUS & MBOX_EMPTY);
        if (MBOX_READ == r)
            return buffer[1] == 0x80000000;
    }
}

void set_uart_clock(void) {
    mbox[0] = 9*4;
    mbox[1] = 0;
    mbox[2] = 0x38002;   // set clock rate tag
    mbox[3] = 12;
    mbox[4] = 8;
    mbox[5] = 2;         // UART clock id
    mbox[6] = 48000000;  // 48 MHz
    mbox[7] = 0;
    mbox[8] = 0;
    mbox_call(MBOX_CH_PROP, (unsigned int *)mbox);
}

void gpio_init_uart(void) {
    // GPIO14,15 ALT0 (PL011)
    GPFSEL1 &= ~((7 << 12) | (7 << 15));
    GPFSEL1 |=  (4 << 12) | (4 << 15);

    GPPUD = 0;
    delay(150);
    GPPUDCLK0 = (1 << 14) | (1 << 15);
    delay(150);
    GPPUDCLK0 = 0;
}

void gpio_init_led(void) {
    // GPIO16 is in GPFSEL1 bits 18–20
    GPFSEL1 &= ~(7 << 18);
    GPFSEL1 |=  (1 << 18);   // set as output
}

void rpi_gpio16_init(void) {
    gpio_init_led();
}

void rpi_gpio16_on(void) {
    GPSET0 = (1 << 16);
}

void rpi_gpio16_off(void) {
    GPCLR0 = (1 << 16);
}

static int rpi_fb_w, rpi_fb_h, rpi_fb_pitch;
static void *rpi_fb_addr;

int rpi_init(void) {
    // Exact sequence from working test
    gpio_init_uart();
    gpio_init_led();
    set_uart_clock();
    return 0;
}

int rpi_gpu_init(void) {
    mbox[0] = 35 * 4;
    mbox[1] = 0;

    // Tag 1: Set physical width/height
    mbox[2]  = 0x48003;
    mbox[3]  = 8;
    mbox[4]  = 0;
    mbox[5]  = 1024;
    mbox[6]  = 768;

    // Tag 2: Set virtual width/height
    mbox[7]  = 0x48004;
    mbox[8]  = 8;
    mbox[9]  = 0;
    mbox[10] = 1024;
    mbox[11] = 768;

    // Tag 3: Set depth
    mbox[12] = 0x48005;
    mbox[13] = 4;
    mbox[14] = 0;
    mbox[15] = 32;

    // Tag 4: Set pixel order
    mbox[16] = 0x48006;
    mbox[17] = 4;
    mbox[18] = 0;
    mbox[19] = 1; // RGB

    // Tag 5: Allocate framebuffer
    mbox[20] = 0x40001;
    mbox[21] = 8;
    mbox[22] = 0;
    mbox[23] = 16; // Alignment
    mbox[24] = 0;

    // Tag 6: Get pitch
    mbox[25] = 0x40008;
    mbox[26] = 4;
    mbox[27] = 0;
    mbox[28] = 0;

    mbox[29] = 0; // End tag

    if (!mbox_call(MBOX_CH_PROP, (unsigned int *)mbox))
        return -1;

    rpi_fb_w = mbox[5];
    rpi_fb_h = mbox[6];
    rpi_fb_pitch = mbox[28];
    rpi_fb_addr = (void *)((uintptr_t)mbox[23] & 0x3FFFFFFF);

    if (rpi_fb_addr) {
        fb_init(rpi_fb_addr, rpi_fb_w, rpi_fb_h, rpi_fb_pitch);
        return 0;
    }
    return -1;
}

void rpi_gpu_flush_rect(int x, int y, int w, int h) {
    if (!rpi_fb_addr || rpi_fb_pitch == 0 || rpi_fb_h == 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > rpi_fb_w) w = rpi_fb_w - x;
    if (y + h > rpi_fb_h) h = rpi_fb_h - y;
    if (w <= 0 || h <= 0) return;

    /* Flush each row in the rectangle */
    for (int ry = y; ry < y + h; ry++) {
        uintptr_t row_start = (uintptr_t)rpi_fb_addr + (ry * rpi_fb_pitch) + (x * 4);
        uintptr_t row_end = row_start + (w * 4);
        
        // Align to 64 bytes
        uintptr_t start = row_start & ~63ULL;
        uintptr_t end = (row_end + 63) & ~63ULL;

        for (uintptr_t p = start; p < end; p += 64) {
            __asm__ volatile("dc cvac, %0" : : "r"(p) : "memory");
        }
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

void rpi_gpu_flush(void) {
    rpi_gpu_flush_rect(0, 0, rpi_fb_w, rpi_fb_h);
}


int rpi_gpu_get_width(void) { return rpi_fb_w ? rpi_fb_w : 1024; }
int rpi_gpu_get_height(void) { return rpi_fb_h ? rpi_fb_h : 768; }

int rpi_input_init(void) { return 0; }
void rpi_input_poll(void) {}

#include "dma.h"
#include "peripherals/emmc.h"

/* ====================================================================
 *  Block Interface Wrappers
 * ==================================================================== */

int rpi_blk_init(void) {
    return emmc_init_card();
}

int rpi_blk_rw(uint64_t sector, void *buf, int write) {
    return emmc_rw(sector, buf, write);
}

