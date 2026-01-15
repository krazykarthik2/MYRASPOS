#include "panic.h"
#include "uart.h"
#include <stdint.h>
#include <stddef.h>

static void print_hex(uintptr_t x) {
    char buf[2 + sizeof(uintptr_t) * 2 + 1];
    int pos = 0;
    buf[pos++] = '0'; buf[pos++] = 'x';
    for (int i = (int)(sizeof(uintptr_t) * 2 - 1); i >= 0; --i) {
        int v = (x >> (i * 4)) & 0xF;
        buf[pos++] = (v < 10) ? ('0' + v) : ('A' + (v - 10));
    }
    buf[pos] = '\0';
    uart_puts(buf);
}

void panic_with_trace(const char *msg) {
    uart_puts("\n[PANIC] ");
    uart_puts(msg);
    uart_puts("\nBacktrace:\n");

    // Walk frame pointers
    void **frame = (void **)__builtin_frame_address(0);
    for (int i = 0; i < 16 && frame; ++i) {
        void *ret = *(frame + 1);
        if (!ret) break;
        uart_puts("  ");
        print_hex((uintptr_t)ret);
        uart_puts("\n");
        frame = (void **)(*frame);
    }

    uart_puts("System halted.\n");
    while (1) ;
}
