#include "uart.h"
#include "sched.h"
#include <stdint.h>

#ifdef REAL
#define UART_BASE 0x3F201000ULL
#define GPIO_BASE 0x3F200000ULL
#else
#define UART_BASE 0x09000000ULL
#endif

#define UART_DR   (UART_BASE + 0x00)
#define UART_FR   (UART_BASE + 0x18)
#define UART_IBRD (UART_BASE + 0x24)
#define UART_FBRD (UART_BASE + 0x28)
#define UART_LCRH (UART_BASE + 0x2C)
#define UART_CR   (UART_BASE + 0x30)
#define UART_ICR  (UART_BASE + 0x44)

#ifdef REAL
#define GPFSEL1   (GPIO_BASE + 0x04)
#define GPPUD     (GPIO_BASE + 0x94)
#define GPPUDCLK0 (GPIO_BASE + 0x98)
#endif


static inline uint32_t mmio_read(uintptr_t reg) {
    return *(volatile uint32_t *)reg;
}

static inline void mmio_write(uintptr_t reg, uint32_t val) {
    *(volatile uint32_t *)reg = val;
}

void uart_init(void) {
#ifdef REAL
    /* 1. GPIO Initialization for UART0 (PL011) */
    /* Pins 14, 15 to ALT0 */
    uint32_t selector = mmio_read(GPFSEL1);
    selector &= ~((7 << 12) | (7 << 15));
    selector |=  (4 << 12) | (4 << 15);
    mmio_write(GPFSEL1, selector);

    mmio_write(GPPUD, 0);
    delay(150);
    mmio_write(GPPUDCLK0, (1 << 14) | (1 << 15));
    delay(150);
    mmio_write(GPPUDCLK0, 0);

    /* 2. UART Initialization */
    mmio_write(UART_CR, 0);         /* Disable UART */
    mmio_write(UART_ICR, 0x7FF);    /* Clear interrupts */

    /* Baud rate: 115200 @ 48MHz clock */
    /* 48000000 / (16 * 115200) = 26.0416... */
    /* IBRD = 26, FBRD = 0.0416 * 64 + 0.5 = 3.16 -> 3 */
    mmio_write(UART_IBRD, 26);
    mmio_write(UART_FBRD, 3);

    mmio_write(UART_LCRH, (3 << 5)); /* 8 bits, no parity, 1 stop bit, no FIFO */
    mmio_write(UART_CR, (1 << 9) | (1 << 8) | 1); /* Enable TX, RX, and UART */
#else
    /* For simulation, ensure UART is enabled just in case */
    mmio_write(UART_CR, (1 << 9) | (1 << 8) | 1);
#endif
}

void _uart_putu(unsigned int u){
    char buf[11];
    int i = 0;
    if(u==0){
        _uart_putc('0');
        return;
    }
    while (u > 0) {
        buf[i++] = (u % 10) + '0';
        u /= 10;
    }
    while (i > 0) {
        _uart_putc(buf[--i]);
    }
}

void _uart_putc(char c) {
    while (mmio_read(UART_FR) & (1 << 5)); /* TXFF bit 5 */
    mmio_write(UART_DR, (unsigned int)c);
}

void _uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n')
            _uart_putc('\r');
        _uart_putc(*s++);
    }
}

char uart_getc(void) {
    /* FR bit 4 == RXFE (receive FIFO empty) */
    while (mmio_read(UART_FR) & (1 << 4)) {
#ifndef NO_SCHED
        yield();
#endif
    }
    unsigned int v = mmio_read(UART_DR);
    return (char)(v & 0xFF);
}

int uart_haschar(void) {
    /* FR bit 4 == RXFE (receive FIFO empty) */
    return !(mmio_read(UART_FR) & (1 << 4));
}

void panic(const char *reason) {
    _uart_puts("\n\033[1;31m[PANIC] \033[0m");
    _uart_puts(reason);
    _uart_puts("\nSystem halted.\n");
    while (1);
}

void _uart_put_hex(unsigned int v) {
    char buf[9];
    const char *hex = "0123456789ABCDEF";
    for (int i = 0; i < 8; ++i) {
        int shift = (7 - i) * 4;
        unsigned int nib = (v >> shift) & 0xF;
        buf[i] = hex[nib];
    }
    buf[8] = '\0';
    _uart_puts(buf);
}
