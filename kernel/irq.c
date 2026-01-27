#include "irq.h"
#include "uart.h"
#include <stdint.h>
#include <stddef.h>
#include "sched.h"

/* Real hardware IRQ entry/exit and VBAR setup.
   This provides the interrupt-driver API, handlers registration,
   and dispatch for timer/uart/usb. */

extern void vectors(void);

#define MAX_IRQ_HANDLERS 16
struct irq_entry { int num; irq_handler_fn fn; void *arg; };
static struct irq_entry handlers[MAX_IRQ_HANDLERS];

/* GIC Distributor Base for QEMU 'virt' machine */
#define GICD_BASE 0x08000000
#define GICD_CTLR 0x00
#define GICD_ISENABLER 0x100
#define GICD_ICENABLER 0x180
#define GICD_ITARGETSR 0x800

/* GIC CPU Interface Base */
#define GICC_BASE 0x08010000
#define GICC_IAR 0x0C
#define GICC_EOIR 0x10
#define GICC_CTLR 0x00
#define GICC_PMR 0x04

void irq_init(void) {
    /* Setup Vector Base Address Register */
    uintptr_t v = (uintptr_t)vectors;
    __asm__ volatile("msr vbar_el1, %0" : : "r"(v));

    for (int i = 0; i < MAX_IRQ_HANDLERS; ++i) handlers[i].fn = NULL;
    
    /* Enable GIC CPU Interface (Group 0 and 1) */
    volatile uint32_t *gicc_ctlr = (volatile uint32_t *)(GICC_BASE + GICC_CTLR);
    volatile uint32_t *gicc_pmr = (volatile uint32_t *)(GICC_BASE + GICC_PMR);
    *gicc_pmr = 0xFF; /* Allow all priorities */
    *gicc_ctlr = 0x3; /* Enable Group 0 and Group 1 */

    /* Enable GIC Distributor */
    volatile uint32_t *gicd_ctlr = (volatile uint32_t *)(GICD_BASE + GICD_CTLR);
    *gicd_ctlr = 0x1;

    /* Mask all SPIs (Shared Peripheral Interrupts, ID >= 32) at the GIC Distributor.
       We only support polling drivers (UART, Virtio) and internal Timer (PPI).
       Unmasked SPIs causing unhandled IRQ storms will hang the kernel.
       GICD_ICENABLERn clears enable bits. Register 0 covers 0-31 (SGI/PPI), 
       Register 1 covers 32-63, etc. We mask 32-1020 (indexes 1-31). */
    volatile uint32_t *icenable = (volatile uint32_t *)(GICD_BASE + GICD_ICENABLER);
    for (int i = 0; i < 32; ++i) {
        icenable[i] = 0xFFFFFFFF;
    }
}

void irq_unmask(int irq_num) {
    /* GICD_IGROUPR: bit per IRQ. Set to 0 for Group 0 (Secure/Non-Secure depending on setup) */
    volatile uint32_t *igroupr = (volatile uint32_t *)(GICD_BASE + 0x080);
    igroupr[irq_num / 32] &= ~(1 << (irq_num % 32));

    /* GICD_ISENABLER: bit per IRQ */
    volatile uint32_t *isenable = (volatile uint32_t *)(GICD_BASE + GICD_ISENABLER);
    isenable[irq_num / 32] = (1 << (irq_num % 32));
    
    /* GICD_ITARGETSR: byte per IRQ (0-3 for SPIs) */
    if (irq_num >= 32) {
        volatile uint8_t *itargetset = (volatile uint8_t *)(GICD_BASE + GICD_ITARGETSR);
        itargetset[irq_num] = 0x01; /* Target CPU 0 */
    }
}

int irq_register(int irq_num, irq_handler_fn fn, void *arg) {
    for (int i = 0; i < MAX_IRQ_HANDLERS; ++i) {
        if (!handlers[i].fn) {
            handlers[i].num = irq_num;
            handlers[i].fn = fn;
            handlers[i].arg = arg;
            
            /* Unmask at GIC */
            irq_unmask(irq_num);
            return 0;
        }
    }
    return -1;
}

/* Dispatch handler for a specific IRQ number */
void irq_dispatch(int irq_num) {
    for (int i = 0; i < MAX_IRQ_HANDLERS; ++i) {
        if (handlers[i].fn && handlers[i].num == irq_num) {
            handlers[i].fn(handlers[i].arg);
        }
    }
}

/* Simplified: check UART input and Virtio and call registered handler */
void irq_poll_and_dispatch(void) {
    if (uart_haschar()) {
        irq_dispatch(1);
    }
    extern void virtio_input_poll(void);
    virtio_input_poll();
}

/* Called from assembly IRQ entry. */
void irq_entry_c(void) {
    volatile uint32_t *gicc_iar = (volatile uint32_t *)(GICC_BASE + GICC_IAR);
    volatile uint32_t *gicc_eoir = (volatile uint32_t *)(GICC_BASE + GICC_EOIR);
    
    uint32_t iar = *gicc_iar;
    uint32_t irq_num = iar & 0x3FF;
    
    if (irq_num < 1022) {
        /* Real IRQ from GIC */
        // uart_puts("[irq] handler for "); uart_put_hex(irq_num); uart_puts("\n");
        irq_dispatch((int)irq_num);
        *gicc_eoir = iar;
    } else {
        /* Fallback for safety/timer tick from PPI if not in IAR */
        scheduler_tick_advance(1);
        irq_poll_and_dispatch();
    }
    
    scheduler_request_preempt();
}
