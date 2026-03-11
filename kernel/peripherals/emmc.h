#ifndef EMMC_H
#define EMMC_H

#include <stdint.h>
#include <stdbool.h>

#define MMIO_BASE 0x3F000000ULL
#define EMMC_BASE (MMIO_BASE + 0x300000)

#define EMMC_ARG2       (*(volatile uint32_t *)(EMMC_BASE + 0x00))
#define EMMC_BLKSIZECNT (*(volatile uint32_t *)(EMMC_BASE + 0x04))
#define EMMC_ARG1       (*(volatile uint32_t *)(EMMC_BASE + 0x08))
#define EMMC_CMDTM      (*(volatile uint32_t *)(EMMC_BASE + 0x0C))
#define EMMC_RESP0      (*(volatile uint32_t *)(EMMC_BASE + 0x10))
#define EMMC_RESP1      (*(volatile uint32_t *)(EMMC_BASE + 0x14))
#define EMMC_RESP2      (*(volatile uint32_t *)(EMMC_BASE + 0x18))
#define EMMC_RESP3      (*(volatile uint32_t *)(EMMC_BASE + 0x1C))
#define EMMC_DATA       (*(volatile uint32_t *)(EMMC_BASE + 0x20))
#define EMMC_STATUS     (*(volatile uint32_t *)(EMMC_BASE + 0x24))
#define EMMC_CONTROL0   (*(volatile uint32_t *)(EMMC_BASE + 0x28))
#define EMMC_CONTROL1   (*(volatile uint32_t *)(EMMC_BASE + 0x2C))
#define EMMC_INTERRUPT  (*(volatile uint32_t *)(EMMC_BASE + 0x30))
#define EMMC_IRPT_MASK  (*(volatile uint32_t *)(EMMC_BASE + 0x34))
#define EMMC_IRPT_EN    (*(volatile uint32_t *)(EMMC_BASE + 0x38))
#define EMMC_CONTROL2   (*(volatile uint32_t *)(EMMC_BASE + 0x3C))

#define SR_CMD_INHIBIT   0x00000001
#define SR_DAT_INHIBIT   0x00000002
#define SR_READ_AVAILABLE 0x00000800
#define SR_WRITE_AVAILABLE 0x00000400

#define INT_CMD_DONE     0x00000001
#define INT_DATA_DONE    0x00000002
#define INT_WRITE_RDY    0x00000010
#define INT_READ_RDY     0x00000020
#define INT_ERROR        0x00008000
#define INT_CMD_TIMEOUT  0x00010000
#define INT_DATA_TIMEOUT 0x00100000

#define C1_RESET_HOST    0x01000000
#define C1_RESET_ALL     (0x01000000 | 0x02000000 | 0x04000000)
#define C1_CLK_INTLEN    0x00000001
#define C1_CLK_STABLE    0x00000002
#define C1_CLK_EN        0x00000004
#define C1_TOUNIT_MAX    0x000E0000

#define EMMC_DEBUG 1
#define SD_CLOCK_NORMAL 25000000
#define SD_CLOCK_ID     400000

typedef enum { RTNone, RT136, RT48, RT48Busy } cmd_resp;

typedef struct {
    uint32_t index;
    uint32_t is_data;
    uint32_t is_app;
    uint32_t direction;
    uint32_t auto_comp;
    uint32_t use_dma;
    cmd_resp response_type;
    uint32_t pad0, pad1, pad2, pad3, pad4, pad5, pad6;
} emmc_cmd;

#define RES_CMD {0,0,0,0,0,0,0,0,0,0,0,0,0,0}
#define TO_REG(cmd) (((cmd)->index << 24) | ((cmd)->response_type << 16) | ((cmd)->is_data << 21) | ((cmd)->direction << 4))

typedef struct {
    bool last_success;
    uint32_t last_command_value;
    emmc_cmd last_command;
    uint32_t last_response[4];
    uint32_t last_error;
    uint32_t last_interrupt;
    uint32_t transfer_blocks;
    uint32_t block_size;
    bool sdhc;
    uint32_t ocr;
    uint32_t rca;
    uint32_t base_clock;
    uint8_t *buffer;
    uint64_t offset;
    struct { uint32_t scr[2]; uint32_t bus_widths; uint32_t version; } scr;
} emmc_device;

extern emmc_device sd_device;
extern uint32_t sd_diskfs_part_lba;

int emmc_init_card(void);
int emmc_rw(uint64_t sector, void *buf, int write);
bool emmc_command(uint32_t index, uint32_t arg, uint32_t timeout);

#endif
