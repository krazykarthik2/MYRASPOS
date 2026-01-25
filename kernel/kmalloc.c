#include "kmalloc.h"
#include "palloc.h"
#include <stdint.h>
#include <stddef.h>
#include "uart.h"

/* 
 * kmalloc implementation improved:
 * - Address-ordered free list for coalescing (merging) adjacent blocks.
 * - Detects double-frees and list corruption.
 */

struct km_header {
    size_t size;           /* Payload size */
    int is_large;          /* 1 if allocated via palloc_alloc_contig directly */
    struct km_header *next;/* For address-ordered free list */
    uint64_t padding;      /* Ensure 16-byte alignment of payload (size is 32) */
};

static struct km_header *free_list = NULL;

void kmalloc_init(void) {
    uart_puts("[kmalloc] Coalescing allocator active\n");
}

static void km_expand_small(void) {
    void *page = palloc_alloc();
    if (!page) {
        uart_puts("[kmalloc] expand_small failed: palloc_alloc returned NULL\n");
        return;
    }
    
    struct km_header *h = (struct km_header *)page;
    h->size = PAGE_SIZE - sizeof(struct km_header); 
    h->is_large = 0;
    h->next = NULL;
    
    /* Insert into address-ordered free list using kfree path */
    kfree((void *)(h + 1));
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    
    // Align to 16 bytes for ARM64 stability
    size = (size + 15) & ~15;
    
    if (size + sizeof(struct km_header) > PAGE_SIZE) {
        // Large allocation
        size_t total_size = size + sizeof(struct km_header);
        size_t pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;
        
        void *ptr = palloc_alloc_contig(pages);
        if (!ptr) return NULL;
        
        struct km_header *h = (struct km_header *)ptr;
        h->size = size;
        h->is_large = (int)pages;
        h->next = NULL;
        
        return (void *)(h + 1);
    }
    
    // Small allocation
    struct km_header **prev = &free_list;
    struct km_header *cur = free_list;
    int limit = 10000; // Cycle detector
    
    while (cur && limit-- > 0) {
        if (cur->size >= size) {
            // Found a block
            if (cur->size >= size + sizeof(struct km_header) + 16) {
                 // Split
                 struct km_header *new_block = (struct km_header *)((uint8_t *)cur + sizeof(struct km_header) + size);
                 new_block->size = cur->size - size - sizeof(struct km_header);
                 new_block->is_large = 0;
                 new_block->next = cur->next;
                 
                 cur->size = size;
                 *prev = new_block;
            } else {
                 // Take whole block
                 *prev = cur->next;
            }
            cur->is_large = 0;
            return (void *)(cur + 1);
        }
        prev = &cur->next;
        cur = cur->next;
    }
    
    if (limit <= 0) {
        uart_puts("[kmalloc] FATAL: Free list cycle detected!\n");
        return NULL;
    }
    
    // No block found, expand
    km_expand_small();
    
    // Try one more time after expansion
    prev = &free_list;
    cur = free_list;
    while (cur) {
        if (cur->size >= size) {
            if (cur->size >= size + sizeof(struct km_header) + 16) {
                 struct km_header *new_block = (struct km_header *)((uint8_t *)cur + sizeof(struct km_header) + size);
                 new_block->size = cur->size - size - sizeof(struct km_header);
                 new_block->is_large = 0;
                 new_block->next = cur->next;
                 cur->size = size;
                 *prev = new_block;
            } else {
                 *prev = cur->next;
            }
            cur->is_large = 0;
            return (void *)(cur + 1);
        }
        prev = &cur->next;
        cur = cur->next;
    }
    
    uart_puts("[kmalloc] failed to allocate "); uart_put_hex((uint32_t)size); uart_puts(" bytes\n");
    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;
    struct km_header *h = (struct km_header *)ptr - 1;
    
    if (h->is_large > 0) {
        palloc_free(h, (size_t)h->is_large);
        return;
    }

    /* Insert into address-ordered free list */
    struct km_header **prev = &free_list;
    struct km_header *cur = free_list;
    
    while (cur && cur < h) {
        prev = &cur->next;
        cur = cur->next;
    }
    
    if (cur == h) {
        uart_puts("[kmalloc] WARNING: Double-free detected at "); uart_put_hex((uint32_t)(uintptr_t)ptr); uart_puts("\n");
        return;
    }

    /* Insert here */
    h->next = cur;
    *prev = h;

    /* Coalesce with NEXT block */
    if (h->next && (uint8_t *)h + sizeof(struct km_header) + h->size == (uint8_t *)h->next) {
        h->size += sizeof(struct km_header) + h->next->size;
        h->next = h->next->next;
    }

    /* Coalesce with PREVIOUS block */
    // Note: *prev is 'h', but finding the block before it requires traversing
    // We can simplify by checking if the block before h (if any) can merge with h.
    // Since we just set *prev = h, the block before is the one whose 'next' pointed to cur.
    // Let's re-traverse or use a cleaner approach.
    // Simpler: find the actual head of the list and traverse to find h's predecessor.
    struct km_header *p = free_list;
    if (p != h) {
        while (p && p->next != h) p = p->next;
        if (p && (uint8_t *)p + sizeof(struct km_header) + p->size == (uint8_t *)h) {
            p->size += sizeof(struct km_header) + h->size;
            p->next = h->next;
        }
    }
}
