#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

typedef void (*irq_handler_fn)(void *arg);

void irq_init(void);
int irq_register(int irq_num, irq_handler_fn fn, void *arg);
/* poll for pending interrupts and dispatch handlers (called from scheduler loop) */
void irq_poll_and_dispatch(void);
/* dispatch a specific interrupt */
void irq_dispatch(int irq_num);

/* entry called from assembly IRQ vector */
/* entry called from assembly IRQ vector */
void irq_entry_c(void);

static inline unsigned long irq_save(void) {
    unsigned long flags;
    __asm__ volatile(
        "mrs %0, daif\n"
        "msr daifset, #2"
        : "=r" (flags)
        :
        : "memory"
    );
    return flags;
}

static inline void irq_restore(unsigned long flags) {
    __asm__ volatile(
        "msr daif, %0"
        :
        : "r" (flags)
        : "memory"
    );
}

#endif
