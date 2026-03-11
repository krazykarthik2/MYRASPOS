#include "dma.h"
#include "palloc.h"
#include "uart.h"
#include <stdint.h>
#include <stdbool.h>

static dma_channel dma_channels[15];
static uint16_t dma_channel_map = 0x1F35;

static uint16_t dma_allocate_channel(uint32_t channel) {
    if (!(channel & ~0x0F)) {
        if (dma_channel_map & (1 << channel)) {
            dma_channel_map &= ~(1 << channel);
            return channel;
        }
        return -1;
    }
    int i = channel == CT_NORMAL ? 6 : 12;
    for (; i >= 0; i--) {
        if (dma_channel_map & (1 << i)) {
            dma_channel_map &= ~(1 << i);
            return i;
        }
    }
    return CT_NONE;
}

// get_free_pages replaced by palloc_alloc

dma_channel *dma_open_channel(uint32_t channel) {
    uint32_t _channel = dma_allocate_channel(channel);
    if (_channel == CT_NONE) {
        _uart_puts("DMA: INVALID CHANNEL!\n");
        return 0;
    }

    dma_channel *dma = &dma_channels[_channel];
    dma->channel = _channel;

    dma->block = (dma_control_block *)palloc_alloc();
    dma->block->res[0] = 0;
    dma->block->res[1] = 0;

    REGS_DMA_ENABLE |= (1 << dma->channel);
    delay(100);
    REGS_DMA(dma->channel)->control |= CS_RESET;
    while(REGS_DMA(dma->channel)->control & CS_RESET) ;
    return dma;
}

void dma_setup_mem_copy(dma_channel *channel, void *dest, void *src, uint32_t length, uint32_t burst_length) {
    channel->block->transfer_info = (burst_length << TI_BURST_LENGTH_SHIFT)
						    | TI_SRC_WIDTH | TI_SRC_INC
						    | TI_DEST_WIDTH | TI_DEST_INC;
    channel->block->src_addr = (uint32_t)(uintptr_t)src;
    channel->block->dest_addr = (uint32_t)(uintptr_t)dest;
    channel->block->transfer_length = length;
    channel->block->mode_2d_stride = 0;
    channel->block->next_block_addr = 0;
}

void dma_start(dma_channel *channel) {
    REGS_DMA(channel->channel)->control_block_addr = (uint32_t)BUS_ADDRESS(channel->block);
    REGS_DMA(channel->channel)->control = CS_WAIT_FOR_OUTSTANDING_WRITES
					      | (DEFAULT_PANIC_PRIORITY << CS_PANIC_PRIORITY_SHIFT)
					      | (DEFAULT_PRIORITY << CS_PRIORITY_SHIFT)
					      | CS_ACTIVE;
}

bool dma_wait(dma_channel *channel) {
    while(REGS_DMA(channel->channel)->control & CS_ACTIVE) ;
    channel->status = REGS_DMA(channel->channel)->control & CS_ERROR ? false : true;
    return channel->status;
}
