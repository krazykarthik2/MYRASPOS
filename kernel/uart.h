#ifndef UART_H
#define UART_H

/* UART and helper prototypes */
void uart_putc(char c);
void uart_puts(const char *s);
void uart_put_hex(unsigned int v);
void delay(unsigned int ticks);
void panic(const char *reason);

#endif /* UART_H */
