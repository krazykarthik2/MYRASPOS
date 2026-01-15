#include "kmalloc.h"
#include "palloc.h"
#include <stdint.h>
#include <stddef.h>

struct km_block {
    size_t size;
    struct km_block *next;
};

static struct km_block *free_list = NULL;

void kmalloc_init(void) {
    // Nothing to do; allocation will pull pages from palloc lazily
}

static void km_expand(void) {
    void *page = palloc_alloc();
    if (!page) return;
    struct km_block *b = (struct km_block *)page;
    b->size = PAGE_SIZE - sizeof(struct km_block);
    b->next = free_list;
    free_list = b;
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    // Align size
    size = (size + 7) & ~7;
    struct km_block **prev = &free_list;
    struct km_block *b = free_list;
    while (b) {
        if (b->size >= size) {
            // For simplicity, return whole block
            *prev = b->next;
            return (void *)((uint8_t *)b + sizeof(struct km_block));
        }
        prev = &b->next;
        b = b->next;
    }
    // No block, expand and retry once
    km_expand();
    b = free_list;
    if (!b) return NULL;
    free_list = b->next;
    return (void *)((uint8_t *)b + sizeof(struct km_block));
}

void kfree(void *ptr) {
    if (!ptr) return;
    struct km_block *b = (struct km_block *)((uint8_t *)ptr - sizeof(struct km_block));
    b->next = free_list;
    free_list = b;
}
