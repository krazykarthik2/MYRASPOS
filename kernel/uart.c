#include "uart.h"
#include <stdint.h>

#define UART_BASE 0x09000000ULL
#define UART_DR   (UART_BASE + 0x00)
#define UART_FR   (UART_BASE + 0x18)

static inline uint32_t mmio_read(uintptr_t reg) {
    return *(volatile uint32_t *)reg;
}

static inline void mmio_write(uintptr_t reg, uint32_t val) {
    *(volatile uint32_t *)reg = val;
}
void uart_putu(unsigned int u){
    char buf[11];
    int i = 0;
    if(u==0){
        uart_putc('0');
        return;
    }
    while (u > 0) {
        buf[i++] = (u % 10) + '0';
        u /= 10;
    }
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

void uart_putc(char c) {
    while (mmio_read(UART_FR) & (1 << 5)); /* TXFF bit 5 */
    mmio_write(UART_DR, (unsigned int)c);
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

char uart_getc(void) {
    /* FR bit 4 == RXFE (receive FIFO empty) */
    while (mmio_read(UART_FR) & (1 << 4)) {
#ifndef NO_SCHED
        extern void yield(void);
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
    uart_puts("\n[PANIC] ");
    uart_puts(reason);
    uart_puts("\nSystem halted.\n");
    while (1);
}

void uart_put_hex(unsigned int v) {
    char buf[9];
    const char *hex = "0123456789ABCDEF";
    for (int i = 0; i < 8; ++i) {
        int shift = (7 - i) * 4;
        unsigned int nib = (v >> shift) & 0xF;
        buf[i] = hex[nib];
    }
    buf[8] = '\0';
    uart_puts(buf);
}
