#include "palloc.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "uart.h"

static size_t total_pages = 0;
static uint8_t *pool_start_addr = NULL;
static uint8_t page_bitmap[PALLOC_MAX_PAGES / 8]; // 1 bit per page 0=free, 1=used

void palloc_init(void *pool_start, size_t pages) {
    if (pages > PALLOC_MAX_PAGES)
        pages = PALLOC_MAX_PAGES;
    
    pool_start_addr = (uint8_t *)pool_start;
    total_pages = pages;
    
    // All free initially
    memset(page_bitmap, 0, sizeof(page_bitmap));
    
    uart_puts("[palloc] pool_start="); uart_put_hex((uintptr_t)pool_start);
    uart_puts(" pages="); uart_put_hex(pages); 
    uart_puts(" max="); uart_put_hex(PALLOC_MAX_PAGES); uart_puts("\n");
}

static int is_free(size_t idx) {
    return !(page_bitmap[idx / 8] & (1 << (idx % 8)));
}

static void mark_used(size_t idx) {
    page_bitmap[idx / 8] |= (1 << (idx % 8));
}

static void mark_free(size_t idx) {
    page_bitmap[idx / 8] &= ~(1 << (idx % 8));
}

void *palloc_alloc_contig(size_t count) {
    if (total_pages == 0) {
        uart_puts("[palloc] ERROR: allocation requested before init or pool empty!\n");
        return NULL;
    }
    
    if (count == 0 || count > total_pages) {
        uart_puts("[palloc] ERROR: invalid count="); uart_put_hex((uint32_t)count);
        uart_puts(" total_pages="); uart_put_hex((uint32_t)total_pages); uart_puts("\n");
        return NULL;
    }
    
    size_t consecutive = 0;
    size_t start_idx = 0;
    
    for (size_t i = 0; i < total_pages; i++) {
        if (is_free(i)) {
            if (consecutive == 0) start_idx = i;
            consecutive++;
            if (consecutive == count) {
                // Found enough!
                for (size_t j = 0; j < count; j++) {
                    mark_used(start_idx + j);
                }
                void *ptr = (void *)(pool_start_addr + start_idx * PAGE_SIZE);
                memset(ptr, 0, count * PAGE_SIZE);
                return ptr;
            }
        } else {
            consecutive = 0;
        }
    }
    
    uart_puts("\n[palloc] CRITICAL: OUT OF MEMORY (CONTIG)! requested="); uart_put_hex((uint32_t)count); uart_puts(" pages\n");
    return NULL;
}

void *palloc_alloc(void) {
    return palloc_alloc_contig(1);
}

void palloc_free(void *ptr, size_t count) {
    if (!ptr || count == 0) return;
    uintptr_t offset = (uintptr_t)ptr - (uintptr_t)pool_start_addr;
    if (offset % PAGE_SIZE != 0) {
        uart_puts("[palloc] free invalid align\n");
        return;
    }
    
    size_t idx = offset / PAGE_SIZE;
    if (idx + count > total_pages) {
         uart_puts("[palloc] free out of bounds\n");
         return;
    }
    
    for (size_t i = 0; i < count; i++) {
        mark_free(idx + i);
    }
}


