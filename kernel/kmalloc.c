#include "kmalloc.h"
#include "palloc.h"
#include <stdint.h>
#include <stddef.h>
#include "uart.h"

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
    
    /* Current kmalloc only supports blocks within a single page pool.
     * Max size is PAGE_SIZE - sizeof(struct km_block) = 4096 - 16 = 4080. */
    if (size > 4080) {
        uart_puts("[kmalloc] ERROR: request too large: "); uart_put_hex((uint32_t)size); uart_puts("\n");
        return NULL;
    }

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
    
    // No block, expand and retry
    km_expand();
    
    // RETRY: start from head again
    prev = &free_list;
    b = free_list;
    while (b) {
        if (b->size >= size) {
            if (b->size >= size + sizeof(struct km_block) + 16) {
                struct km_block *new_block = (struct km_block *)((uint8_t *)b + sizeof(struct km_block) + size);
                new_block->size = b->size - size - sizeof(struct km_block);
                new_block->next = b->next;
                b->size = size;
                *prev = new_block;
            } else {
                *prev = b->next;
            }
            return (void *)((uint8_t *)b + sizeof(struct km_block));
        }
        prev = &b->next;
        b = b->next;
    }

    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;
    struct km_block *b = (struct km_block *)((uint8_t *)ptr - sizeof(struct km_block));
    b->next = free_list;
    free_list = b;
}
