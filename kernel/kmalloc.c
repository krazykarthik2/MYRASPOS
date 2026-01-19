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
            /* Check if split is possible */
            if (b->size >= size + sizeof(struct km_block) + 16) {
                /* Split */
                struct km_block *new_block = (struct km_block *)((uint8_t *)b + sizeof(struct km_block) + size);
                new_block->size = b->size - size - sizeof(struct km_block);
                new_block->next = b->next;
                
                /* Update allocated block size */
                b->size = size;
                
                /* Replace 'b' with 'new_block' in the free list */
                *prev = new_block;
            } else {
                /* No split, unlink whole block */
                *prev = b->next;
            }
            return (void *)((uint8_t *)b + sizeof(struct km_block));
        }
        prev = &b->next;
        b = b->next;
    }
    // No block, expand and retry once
    // No block, try to expand
    km_expand();
    b = free_list;
    if (!b) return NULL;
    
    // Now that we have a block, try the loop again to respect splitting logic
    // We can just fall through to a second pass or duplicte logic.
    // Simplest is to check the fresh block immediately.
    if (b->size >= size + sizeof(struct km_block) + 16) {
        struct km_block *new_block = (struct km_block *)((uint8_t *)b + sizeof(struct km_block) + size);
        new_block->size = b->size - size - sizeof(struct km_block);
        new_block->next = b->next;
        b->size = size;
        free_list = new_block;
    } else {
        free_list = b->next;
    }
    return (void *)((uint8_t *)b + sizeof(struct km_block));
}

void kfree(void *ptr) {
    if (!ptr) return;
    struct km_block *b = (struct km_block *)((uint8_t *)ptr - sizeof(struct km_block));
    b->next = free_list;
    free_list = b;
}
