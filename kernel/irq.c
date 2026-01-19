#include "irq.h"
#include "uart.h"
#include <stdint.h>
#include <stddef.h>
#include "sched.h"

/* Very small IRQ "dispatcher" implemented as a polled loop to keep
   the existing code simple. Real hardware IRQ entry/exit and VBAR
   setup is intentionally left as a follow-up; this provides the
   interrupt-driver API, handlers registration, and dispatch for
   timer/uart/usb by polling their MMIO state. */

#define MAX_IRQ_HANDLERS 16
struct irq_entry { int num; irq_handler_fn fn; void *arg; };
static struct irq_entry handlers[MAX_IRQ_HANDLERS];

/* GIC Distributor Base for QEMU 'virt' machine */
#define GICD_BASE 0x08000000
#define GICD_ICENABLER 0x180

void irq_init(void) {
    for (int i = 0; i < MAX_IRQ_HANDLERS; ++i) handlers[i].fn = NULL;
    
    /* Mask all SPIs (Shared Peripheral Interrupts, ID >= 32) at the GIC Distributor.
       We only support polling drivers (UART, Virtio) and internal Timer (PPI).
       Unmasked SPIs causing unhandled IRQ storms will hang the kernel.
       GICD_ICENABLERn clears enable bits. Register 0 covers 0-31 (SGI/PPI), 
       Register 1 covers 32-63, etc. We mask 32-1020 (indexes 1-31). */
    volatile uint32_t *icenable = (volatile uint32_t *)(GICD_BASE + GICD_ICENABLER);
    for (int i = 1; i < 32; ++i) {
        icenable[i] = 0xFFFFFFFF;
    }
}

int irq_register(int irq_num, irq_handler_fn fn, void *arg) {
    for (int i = 0; i < MAX_IRQ_HANDLERS; ++i) {
        if (!handlers[i].fn) {
            handlers[i].num = irq_num;
            handlers[i].fn = fn;
            handlers[i].arg = arg;
            return 0;
        }
    }
    return -1;
}

/* Simplified: check UART input and call registered handler with irq_num 1 */
void irq_poll_and_dispatch(void) {
    /* UART IRQ (soft): if data available, dispatch any handler registered for irq_num == 1 */
    if (uart_haschar()) {
        for (int i = 0; i < MAX_IRQ_HANDLERS; ++i) {
            if (handlers[i].fn && handlers[i].num == 1) {
                handlers[i].fn(handlers[i].arg);
            }
        }
    }
    /* USB IRQ stub: dispatch irq_num == 2 if any (no real USB hardware here) */
    /* Timer IRQ handled by timer_poll_and_advance / scheduler wake logic */
}

/* Called from assembly IRQ entry. Request a scheduler preemption and
   advance a software tick. Keep this function small and safe. */
void irq_entry_c(void) {
    /* Advance scheduler tick by 1ms and request preempt */
    scheduler_tick_advance(1);
    scheduler_request_preempt();
}
