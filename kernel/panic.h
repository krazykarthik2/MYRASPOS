#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>
#include "irq.h"

void panic_with_trace(const char *msg);
void exception_c_handler(int type, uint64_t esr, uint64_t elr, struct pt_regs *regs);

#endif
