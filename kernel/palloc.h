#ifndef PALLOC_H
#define PALLOC_H

#include <stddef.h>

#define PAGE_SIZE 4096
#define PALLOC_MAX_PAGES 4096

void palloc_init(void *pool_start, size_t pages);
void *palloc_alloc(void);
void palloc_free(void *page);

#endif
