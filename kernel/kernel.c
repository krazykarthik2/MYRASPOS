#include "kernel.h"
#include "uart.h"
void uart_puts(const char *);  // force prototype

void kernel_main(void) {
    const char *ptr = "Hello";
    char test_msg[] = {'H', 'e', 'l', 'l', 'o', '\0'};
    uart_puts(test_msg);
    // 1. Print a manual string (We know this works)
    char hi[] = "Hi";
    uart_puts(hi);

    // 2. Print the ADDRESS of the "Hello" string in hex
    // If this prints 0x4000xxxx, the pointer is correct.
    // If it prints something like 0x0000xxxx, your linker is wrong.
    unsigned int addr = (unsigned int)ptr;
    for(int i = 28; i >= 0; i -= 4) {
        unsigned int nibble = (addr >> i) & 0xF;
        uart_putc(nibble < 10 ? '0' + nibble : 'A' + (nibble - 10));
    }
    
    uart_putc('\n');
    uart_puts(ptr); // Try again
    while (1);
}