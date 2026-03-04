#ifndef UART_H
#define UART_H

/* Internal UART functions (always available for critical use like panic) */
void _uart_putc(char c);
void _uart_puts(const char *s);
void _uart_put_hex(unsigned int v);
void _uart_putu(unsigned int u);

/* UART Initialization and Control */
void uart_init(void);
void delay(unsigned int ticks);
void panic(const char *reason);
char uart_getc(void);
int uart_haschar(void);

/* Conditional Logging Macros */
#ifdef DEBUG
    #define uart_putc(c) _uart_putc(c)
    #define uart_puts(s) _uart_puts(s)
    #define uart_put_hex(v) _uart_put_hex(v)
    #define uart_putu(u) _uart_putu(u)
    
    #define DEBUG_PRINT(msg) do { \
        _uart_puts("\033[1;33m[DEBUG] "); \
        _uart_puts(msg); \
        _uart_puts("\033[0m\n"); \
    } while(0)
#else
    #define uart_putc(c) ((void)0)
    #define uart_puts(s) ((void)0)
    #define uart_put_hex(v) ((void)0)
    #define uart_putu(u) ((void)0)
    #define DEBUG_PRINT(msg) do {} while(0)
#endif

#endif /* UART_H */
