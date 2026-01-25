#ifndef MMU_H
#define MMU_H

#include <stdint.h>
#include <stddef.h>

/* Page Table Entry Flags */
#define PTE_VALID           (1ULL << 0)
#define PTE_TABLE           (1ULL << 1)
#define PTE_BLOCK           (0ULL << 1)
#define PTE_PAGE            (1ULL << 1)   /* Bit 1 is 1 for L3 entries too */
#define PTE_AF              (1ULL << 10)  /* Access Flag */
#define PTE_SH_INNER        (3ULL << 8)   /* Inner Shareable */
#define PTE_MEMATTR_NORMAL  (0ULL << 2)   /* MAIR index 0 */
#define PTE_MEMATTR_DEVICE  (1ULL << 2)   /* MAIR index 1 */
#define PTE_USER            (1ULL << 6)   /* AP[1] = 1 */
#define PTE_RDONLY          (1ULL << 7)   /* AP[2] = 1 */
#define PTE_PXN             (1ULL << 53)  /* Privileged Execute Never */
#define PTE_UXN             (1ULL << 54)  /* Unprivileged Execute Never */

/* MAIR Attributes */
#define MAIR_DEVICE_nGnRnE  0x00ULL
#define MAIR_NORMAL_WB      0xFFULL
#define MAIR_VALUE          ((MAIR_NORMAL_WB << 0) | (MAIR_DEVICE_nGnRnE << 8))

/* TCR Flags */
#define TCR_T0SZ(n)         ((64ULL - (n)) << 0)
#define TCR_IRGN0_WBWA      (1ULL << 8)
#define TCR_ORGN0_WBWA      (1ULL << 10)
#define TCR_SH0_INNER       (3ULL << 12)
#define TCR_TG0_4KB         (0ULL << 14)
#define TCR_VALUE           (TCR_T0SZ(48) | TCR_IRGN0_WBWA | TCR_ORGN0_WBWA | TCR_SH0_INNER | TCR_TG0_4KB)

void mmu_init(void);
void mmu_map(uintptr_t va, uintptr_t pa, size_t size, uint64_t flags);

#endif
