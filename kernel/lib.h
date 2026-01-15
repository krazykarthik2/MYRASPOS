#ifndef KERNEL_LIB_H
#define KERNEL_LIB_H

#include <stddef.h>

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
char *strncpy(char *dest, const char *src, size_t n);
int strncmp(const char *a, const char *b, size_t n);

#endif
