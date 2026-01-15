#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

typedef uintptr_t (*syscall_fn)(uintptr_t, uintptr_t, uintptr_t);

void syscall_init(void);
int syscall_register(uint32_t num, syscall_fn fn);
uintptr_t syscall_handle(uint32_t num, uintptr_t a0, uintptr_t a1, uintptr_t a2);

#endif
