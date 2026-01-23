#include "kmalloc.h"
#include "palloc.h"
#include <stdint.h>
#include <stddef.h>
#include "uart.h"

/* 
 * kmalloc implementation improved:
 * - Small allocations (<= 2048): Use a slab-like allocator inside 4KB pages.
 * - Large allocations (> 2048): Allocate contiguous pages directly from palloc.
 */

struct km_header {
    size_t size;           /* Payload size */
    int is_large;          /* 1 if allocated via palloc_alloc_contig directly */
    struct km_header *next;/* For free list */
    uint64_t padding;      /* Ensure 16-byte alignment of payload (size is now 32) */
};

/* Free list for small blocks.
   We only keep track of free blocks. Used blocks are just chunks. */
static struct km_header *free_list = NULL;

void kmalloc_init(void) {
    uart_puts("[kmalloc] 16-byte alignment bridge active (header=32)\n");
}

static void km_expand_small(void) {
    void *page = palloc_alloc();
    if (!page) return;
    
    struct km_header *h = (struct km_header *)page;
    /* Usable size is PAGE_SIZE - header */
    h->size = PAGE_SIZE - sizeof(struct km_header); 
    h->is_large = 0;
    h->next = free_list;
    free_list = h;
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    
    // Align to 16 bytes for ARM64 stability (ABI requirement for stacks and pairs)
    size = (size + 15) & ~15;
    
    // Threshold for "Large" allocation
    // If request + header > 4096, must be large.
    // Let's set threshold at 2048 to easily fit in page, 
    // but actually we can support up to 4096-header.
    // Simpler: if size + sizeof(header) > PAGE_SIZE, use large.
    
    if (size + sizeof(struct km_header) > PAGE_SIZE) {
        // Large allocation
        size_t total_size = size + sizeof(struct km_header);
        size_t pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;
        
        void *ptr = palloc_alloc_contig(pages);
        if (!ptr) {
            uart_puts("[kmalloc] Failed large alloc: "); uart_put_hex(size); uart_puts("\n");
            return NULL;
        }
        
        struct km_header *h = (struct km_header *)ptr;
        h->size = size; // or pages * PAGE_SIZE? user only cares about requested size
        h->is_large = (int)pages; // Store number of pages in is_large field (if > 0)
        h->next = NULL;
        
        return (void *)(h + 1);
    }
    
    // Small allocation
    struct km_header **prev = &free_list;
    struct km_header *cur = free_list;
    
    while (cur) {
        if (cur->size >= size) {
            // Found a block
            // Can we split?
            if (cur->size >= size + sizeof(struct km_header) + 16) {
                 struct km_header *new_block = (struct km_header *)((uint8_t *)cur + sizeof(struct km_header) + size);
                 new_block->size = cur->size - size - sizeof(struct km_header);
                 new_block->is_large = 0;
                 new_block->next = cur->next;
                 
                 cur->size = size;
                 *prev = new_block;
            } else {
                 // Don't split
                 *prev = cur->next;
            }
            cur->is_large = 0;
            return (void *)(cur + 1);
        }
        prev = &cur->next;
        cur = cur->next;
    }
    
    // No block found, expand
    km_expand_small();
    
    // Retry once (guaranteed to be at head)
    cur = free_list;
    if (cur && cur->size >= size) {
         if (cur->size >= size + sizeof(struct km_header) + 16) {
             struct km_header *new_block = (struct km_header *)((uint8_t *)cur + sizeof(struct km_header) + size);
             new_block->size = cur->size - size - sizeof(struct km_header);
             new_block->is_large = 0;
             new_block->next = cur->next;
             cur->size = size;
             free_list = new_block;
         } else {
             free_list = cur->next;
         }
         cur->is_large = 0;
         return (void *)(cur + 1);
    }
    
    uart_puts("[kmalloc] OOM small alloc\n");
    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;
    struct km_header *h = (struct km_header *)ptr - 1;
    
    if (h->is_large > 0) {
        // Large allocation, is_large stores page count
        palloc_free(h, (size_t)h->is_large);
    } else {
        // Small allocation, return to free list
        // Simple insertion at head. No coalescing implemented yet, but better than nothing.
        h->next = free_list;
        free_list = h;
    }
}
