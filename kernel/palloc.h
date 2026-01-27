#ifndef PALLOC_H
#define PALLOC_H

#include <stddef.h>

#define PAGE_SIZE 4096
#define PALLOC_MAX_PAGES (256 * 1024) /* 1GB max */

void palloc_init(void *pool_start, size_t pages);
void *palloc_alloc(void); /* allocate 1 page */
void *palloc_alloc_contig(size_t count); /* allocate 'count' contiguous pages */
void palloc_free(void *ptr, size_t count); /* free pages */
size_t palloc_get_free_pages(void);

/* helper for legacy 1-page free */
static inline void palloc_free_one(void *ptr) {
    palloc_free(ptr, 1);
}

#endif
