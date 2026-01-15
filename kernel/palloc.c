#include "palloc.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static void *free_list[PALLOC_MAX_PAGES];
static size_t free_count = 0;
static size_t total_pages = 0;

void palloc_init(void *pool_start, size_t pages) {
    if (pages > PALLOC_MAX_PAGES)
        pages = PALLOC_MAX_PAGES;
    total_pages = pages;
    free_count = pages;
    for (size_t i = 0; i < pages; ++i) {
        free_list[i] = (void *)((uintptr_t)pool_start + i * PAGE_SIZE);
    }
}

void *palloc_alloc(void) {
    if (free_count == 0)
        return NULL;
    void *page = free_list[--free_count];
    // zero page
    memset(page, 0, PAGE_SIZE);
    return page;
}

void palloc_free(void *page) {
    if (free_count >= PALLOC_MAX_PAGES)
        return;
    free_list[free_count++] = page;
}
