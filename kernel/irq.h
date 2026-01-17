#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

typedef void (*irq_handler_fn)(void *arg);

void irq_init(void);
int irq_register(int irq_num, irq_handler_fn fn, void *arg);
/* poll for pending interrupts and dispatch handlers (called from scheduler loop) */
void irq_poll_and_dispatch(void);

#endif
