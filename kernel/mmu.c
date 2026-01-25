#include "mmu.h"
#include "palloc.h"
#include "uart.h"
#include "lib.h"
#include <string.h>

extern char __bss_start[];
extern char __bss_end[];

static uint64_t *kernel_l0 = NULL;

static uint64_t *get_next_level(uint64_t *current_table, int index) {
    if (!(current_table[index] & PTE_VALID)) {
        uint64_t *next = (uint64_t *)palloc_alloc();
        if (!next) return NULL;
        memset(next, 0, PAGE_SIZE);
        
        /* Flush next table to RAM */
        uintptr_t p = (uintptr_t)next;
        for (uintptr_t i = 0; i < PAGE_SIZE; i += 64) {
            __asm__ volatile("dc civac, %0" : : "r" (p + i) : "memory");
        }
        __asm__ volatile("dmb sy" ::: "memory");

        current_table[index] = (uintptr_t)next | PTE_TABLE | PTE_VALID;

        /* Flush current table update to RAM */
        __asm__ volatile("dc civac, %0" : : "r" (&current_table[index]) : "memory");
        __asm__ volatile("dmb sy" ::: "memory");

        return next;
    }
    return (uint64_t *)(current_table[index] & ~0xFFFULL);
}

void mmu_map(uintptr_t va, uintptr_t pa, size_t size, uint64_t flags) {
    if (!kernel_l0) return;

    uintptr_t v_start = va & ~0xFFFULL;
    uintptr_t v_end = (va + size + 0xFFFULL) & ~0xFFFULL;
    uintptr_t p_ptr = pa & ~0xFFFULL;

    for (uintptr_t v_ptr = v_start; v_ptr < v_end; v_ptr += PAGE_SIZE) {
        int l0_idx = (v_ptr >> 39) & 0x1FF;
        int l1_idx = (v_ptr >> 30) & 0x1FF;
        int l2_idx = (v_ptr >> 21) & 0x1FF;
        int l3_idx = (v_ptr >> 12) & 0x1FF;

        uint64_t *l1 = get_next_level(kernel_l0, l0_idx);
        if (!l1) return;
        uint64_t *l2 = get_next_level(l1, l1_idx);
        if (!l2) return;
        uint64_t *l3 = get_next_level(l2, l2_idx);
        if (!l3) return;

        l3[l3_idx] = p_ptr | flags | PTE_VALID | PTE_PAGE;
        
        /* Flush L3 update */
        __asm__ volatile("dc civac, %0" : : "r" (&l3[l3_idx]) : "memory");

        p_ptr += PAGE_SIZE;
    }
    __asm__ volatile("dmb sy" ::: "memory");
}

void mmu_init(void) {
    kernel_l0 = (uint64_t *)palloc_alloc();
    memset(kernel_l0, 0, PAGE_SIZE);

    /* Flush L0 table base to RAM */
    uintptr_t p = (uintptr_t)kernel_l0;
    for (uintptr_t i = 0; i < PAGE_SIZE; i += 64) {
        __asm__ volatile("dc civac, %0" : : "r" (p + i) : "memory");
    }
    __asm__ volatile("dmb sy" ::: "memory");

    /* 
     * Map specifically what we need to avoid using too many page tables:
     * 0x00000000 to 0x40000000 (1GB): Peripheral space (DEVICE)
     * 0x40000000 to 0x60000000 (512MB): RAM space (NORMAL) - covers kernel and basic heap
     */
    
    // Peripherals (UART, GIC, Virtio, etc.)
    mmu_map(0, 0, 0x40000000, PTE_AF | PTE_MEMATTR_DEVICE);

    // RAM (Kernel, BSS, Stack, Framebuffer)
    mmu_map(0x40000000, 0x40000000, 0x20000000, PTE_AF | PTE_SH_INNER | PTE_MEMATTR_NORMAL);

    uart_puts("[mmu] Page tables set up. Invalidating BSS...\n");
    
    /* CRITICAL: Invalidate the BSS section (where static globals like palloc state live).
       This ensures that the MMU sees the data written to RAM during palloc_init. */
    uintptr_t bss_start = (uintptr_t)__bss_start;
    uintptr_t bss_end = (uintptr_t)__bss_end;
    
    /* Ensure 64-byte alignment or handle unaligned start/end if needed, 
       but linker usually aligns sections. */
    for (uintptr_t addr = bss_start; addr < bss_end; addr += 64) {
        __asm__ volatile("dc civac, %0" : : "r" (addr) : "memory");
    }
    __asm__ volatile("dmb sy" ::: "memory");
    
    uart_puts("[mmu] Enabling MMU...\n");

    /* Invalidate TLBs */
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");

    /* Set up system registers */
    __asm__ volatile("msr mair_el1, %0" : : "r" (MAIR_VALUE));
    
    /* Simpler TCR: 48-bit VA, 4KB granule, inner shareable WBWA */
    uint64_t tcr = (16ULL << 0) |   /* T0SZ = 64-48 */
                    (1ULL << 8) |   /* IRGN0 = WBWA */
                    (1ULL << 10) |  /* ORGN0 = WBWA */
                    (3ULL << 12) |  /* SH0 = Inner Shareable */
                    (0ULL << 14);   /* TG0 = 4KB */
    
    __asm__ volatile("msr tcr_el1, %0" : : "r" (tcr));
    __asm__ volatile("msr ttbr0_el1, %0" : : "r" (kernel_l0));
    __asm__ volatile("isb");

    /* Enable MMU */
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r" (sctlr));
    sctlr |= (1ULL << 0);  /* M bit (MMU enable) */
    sctlr |= (1ULL << 12); /* I bit (Instruction cache enable) */
    sctlr |= (1ULL << 2);  /* C bit (Data cache enable) */
    
    __asm__ volatile("dsb sy");
    __asm__ volatile("msr sctlr_el1, %0" : : "r" (sctlr));
    __asm__ volatile("isb");

    uart_puts("[mmu] MMU enabled.\n");
}

