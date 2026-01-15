#include "uart.h"

#define UART_BASE 0x09000000
#define UART_DR   (UART_BASE + 0x00)
#define UART_FR   (UART_BASE + 0x18)

static inline unsigned int mmio_read(unsigned int reg) {
    return *(volatile unsigned int *)reg;
}

static inline void mmio_write(unsigned int reg, unsigned int val) {
    *(volatile unsigned int *)reg = val;
}

void uart_putc(char c) {
#ifndef qemu
    while (mmio_read(UART_FR) & (1 << 5));
#endif
    mmio_write(UART_DR, (unsigned int)c);
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

void panic(const char *reason) {
    uart_puts("\n[PANIC] ");
    uart_puts(reason);
    uart_puts("\nSystem halted.\n");
    while (1);
}
