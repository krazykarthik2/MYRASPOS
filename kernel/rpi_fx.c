#include "rpi_fx.h"
#include "uart.h"
#include "framebuffer.h"
#include "input.h"
#include <stdint.h>

/* RPi Peripheral Base (BCM2837 / Pi Zero 2 W) */
#define PBASE 0x3F000000ULL
#define GPIO_BASE (PBASE + 0x00200000)

#define GPIO_pin(pin) (*(volatile uint32_t*)(GPIO_BASE + ((pin)/10)*4))
#define GPIO_ONBOARDLED 47


#define GPFSEL4   ((volatile unsigned int*)(GPIO_BASE + 0x10))
#define GPSET1    ((volatile unsigned int*)(GPIO_BASE + 0x20))
#define GPCLR1    ((volatile unsigned int*)(GPIO_BASE + 0x2C))

void delay(unsigned int ticks) {
    for (volatile unsigned int i = 0; i < ticks; i++);
}

void rpi_built_in_led_on(void) {
    /* LED ON (active LOW) */
    *GPCLR1 = (1 << (GPIO_ONBOARDLED - 32));
}

void rpi_built_in_led_off(void) {
    /* LED OFF */
    *GPSET1 = (1 << (GPIO_ONBOARDLED - 32));
}


/* Mailbox Registers */
#define MBOX_BASE    (PBASE + 0x0000B880)
#define MBOX_READ    ((volatile uint32_t*)(MBOX_BASE + 0x00))
#define MBOX_STATUS  ((volatile uint32_t*)(MBOX_BASE + 0x18))
#define MBOX_WRITE   ((volatile uint32_t*)(MBOX_BASE + 0x20))

#define MBOX_RESPONSE 0x80000000
#define MBOX_FULL     0x80000000
#define MBOX_EMPTY    0x40000000

/* Mailbox Property Interface Tags */
#define MBOX_REQUEST          0x00000000
#define MBOX_CH_PROP          8
#define MBOX_TAG_SET_PHYS_WH  0x00048003
#define MBOX_TAG_SET_VIRT_WH  0x00048004
#define MBOX_TAG_SET_DEPTH    0x00048005
#define MBOX_TAG_ALLOC_BUFFER 0x00040001
#define MBOX_TAG_GET_PITCH    0x00040008
#define MBOX_TAG_LAST         0x00000000

/* Aligned buffer for mailbox messages */
static uint32_t __attribute__((aligned(16))) mbox[36];

static int mbox_call(unsigned char ch) {
    uint32_t r = (((uint32_t)((uintptr_t)&mbox) & ~0xF) | (ch & 0xF));
    
    /* Clean cache for the mailbox buffer */
    for (uintptr_t i = (uintptr_t)&mbox; i < (uintptr_t)&mbox + sizeof(mbox); i += 64) {
        __asm__ volatile("dc civac, %0" : : "r" (i) : "memory");
    }
    __asm__ volatile("dmb sy" ::: "memory");

    /* Wait until we can write */
    while (*MBOX_STATUS & MBOX_FULL);
    
    /* Write the address to the mailbox */
    *MBOX_WRITE = r;

    /* Wait for response */
    while (1) {
        while (*MBOX_STATUS & MBOX_EMPTY);
        if (r == *MBOX_READ) {
            /* Invalidate cache before reading response */
            for (uintptr_t i = (uintptr_t)&mbox; i < (uintptr_t)&mbox + sizeof(mbox); i += 64) {
                __asm__ volatile("dc ivac, %0" : : "r" (i) : "memory");
            }
            __asm__ volatile("dmb sy" ::: "memory");
            return mbox[1] == MBOX_RESPONSE;
        }
    }
    return 0;
}

static int rpi_fb_w, rpi_fb_h, rpi_fb_pitch;
static void *rpi_fb_addr;

int rpi_init(void) {
    uart_puts("[rpi] rpi_init: Raspberry Pi Zero 2 W Hardware Init\n");
    return 0;
}

int rpi_gpu_init(void) {
    uart_puts("[rpi] Initializing GPU via Mailbox...\n");

    /* -------- Set GPIO47 as OUTPUT -------- */
    unsigned int val = *GPFSEL4;
    val &= ~(7 << 21);   /* clear bits for pin 47 (47-40=7, 7*3=21) */
    val |=  (1 << 21);   /* set as output (001) */
    *GPFSEL4 = val;

    /* -------- 5 Second Blink Sequence -------- */
    /* Assuming rpi_delay(500000) is roughly 0.25-0.5s on this hardware */
    /* We'll do 10 cycles of 0.5s (0.25 on, 0.25 off) for ~5 seconds */
    uart_puts("[rpi] Blinking built-in LED for 5 seconds...\n");
    for(int i = 0; i < 10; i++) {
        rpi_built_in_led_on();
        delay(1000000); // 0.25s
        rpi_built_in_led_off();
        delay(1000000); // 0.25s
    }

    mbox[0] = 35 * 4;
    mbox[1] = MBOX_REQUEST;

    mbox[2] = MBOX_TAG_SET_PHYS_WH;
    mbox[3] = 8;
    mbox[4] = 8;
    mbox[5] = 1024;
    mbox[6] = 768;

    mbox[7] = MBOX_TAG_SET_VIRT_WH;
    mbox[8] = 8;
    mbox[9] = 8;
    mbox[10] = 1024;
    mbox[11] = 768;

    mbox[12] = MBOX_TAG_SET_DEPTH;
    mbox[13] = 4;
    mbox[14] = 4;
    mbox[15] = 32;

    mbox[16] = MBOX_TAG_ALLOC_BUFFER;
    mbox[17] = 8;
    mbox[18] = 8;
    mbox[19] = 4096; /* Alignment */
    mbox[20] = 0;    /* Framebuffer address will be here */

    mbox[21] = MBOX_TAG_GET_PITCH;
    mbox[22] = 4;
    mbox[23] = 4;
    mbox[24] = 0;    /* Pitch will be here */

    mbox[25] = MBOX_TAG_LAST;

    if (mbox_call(MBOX_CH_PROP) && mbox[15] == 32 && mbox[20] != 0) {
        /* Success! */
        rpi_fb_addr = (void *)((uintptr_t)mbox[20] & 0x3FFFFFFF); /* Convert GPU address to physical */
        rpi_fb_w = mbox[5];
        rpi_fb_h = mbox[6];
        rpi_fb_pitch = mbox[24];

        uart_puts("[rpi] Framebuffer allocated at: ");
        uart_put_hex((uint32_t)(uintptr_t)rpi_fb_addr);
        uart_puts("\n");

        fb_init(rpi_fb_addr, rpi_fb_w, rpi_fb_h, rpi_fb_pitch);
        return 0;
    }

    uart_puts("[rpi] Error: GPU initialization failed!\n");
    return -1;
}

void rpi_gpu_flush(void) {
    /* No-op on real RPi with direct FB */
}

int rpi_gpu_get_width(void) {
    return rpi_fb_w ? rpi_fb_w : 1024;
}

int rpi_gpu_get_height(void) {
    return rpi_fb_h ? rpi_fb_h : 768;
}

int rpi_input_init(void) {
    uart_puts("[rpi] rpi_input_init: Using UART for keyboard input fallback\n");
    return 0;
}

void rpi_input_poll(void) {
    /* Fallback: Read from UART and push to input system */
    while (uart_haschar()) {
        char c = uart_getc();
        /* Minimal mapping for ASCII to basic scan codes if needed, 
           or just push as generic keys. */
        input_push_event(INPUT_TYPE_KEY, (uint16_t)c, 1);
        input_push_event(INPUT_TYPE_KEY, (uint16_t)c, 0);
    }
}

int rpi_blk_init(void) {
    uart_puts("[rpi] rpi_blk_init: SD Card support not implemented (EMC stub)\n");
    return -1;
}

int rpi_blk_rw(uint64_t sector, void *buf, int write) {
    (void)sector; (void)buf; (void)write;
    return -1;
}
