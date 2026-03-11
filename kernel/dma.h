#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include <stdbool.h>

#define REGS_DMA_BASE 0x3F007000
#define REGS_DMA_ENABLE (*(volatile uint32_t*)(REGS_DMA_BASE + 0xFF0))

#define CS_RESET (1 << 31)
#define CS_ACTIVE (1 << 0)
#define CS_ERROR (1 << 8)
#define CS_WAIT_FOR_OUTSTANDING_WRITES (1 << 28)
#define CS_PANIC_PRIORITY_SHIFT 20
#define CS_PRIORITY_SHIFT 16

#define DEFAULT_PANIC_PRIORITY 15
#define DEFAULT_PRIORITY 1

#define TI_BURST_LENGTH_SHIFT 12
#define TI_SRC_WIDTH (1 << 9)
#define TI_SRC_INC (1 << 8)
#define TI_DEST_WIDTH (1 << 5)
#define TI_DEST_INC (1 << 4)

#define BUS_ADDRESS(x) (((uintptr_t)(x)) | 0xC0000000ULL)

#define CT_NORMAL 0
#define CT_NONE 0xFFFF

typedef struct {
    uint32_t transfer_info;
    uint32_t src_addr;
    uint32_t dest_addr;
    uint32_t transfer_length;
    uint32_t mode_2d_stride;
    uint32_t next_block_addr;
    uint32_t res[2];
} dma_control_block;

typedef struct {
    uint16_t channel;
    dma_control_block *block;
    bool status;
} dma_channel;

typedef struct {
    volatile uint32_t control;
    volatile uint32_t control_block_addr;
    volatile uint32_t transfer_info;
    volatile uint32_t src_addr;
    volatile uint32_t dest_addr;
    volatile uint32_t transfer_length;
    volatile uint32_t mode_2d_stride;
    volatile uint32_t next_block_addr;
    volatile uint32_t debug;
} dma_req_regs;

#define REGS_DMA(ch) ((dma_req_regs*)((uintptr_t)REGS_DMA_BASE + (uintptr_t)(ch)*0x100))

dma_channel *dma_open_channel(uint32_t channel);
void dma_setup_mem_copy(dma_channel *channel, void *dest, void *src, uint32_t length, uint32_t burst_length);
void dma_start(dma_channel *channel);
bool dma_wait(dma_channel *channel);

#endif
