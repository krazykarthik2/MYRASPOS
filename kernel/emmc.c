#include "peripherals/emmc.h"
#include "rpi_fx.h"
#include "uart.h"
#include <stdint.h>
#include <stdbool.h>

emmc_device sd_device = {0};
uint32_t sd_diskfs_part_lba = 0;
uint32_t sd_debug_status = 0;

static const emmc_cmd commands[] = {
    {0, 0, 0, 0, 0, 0, RTNone, 0, 0, 0, 0, 0, 0, 0},
    RES_CMD,
    {2, 0, 0, 0, 0, 0, RT136, 0, 0, 0, 0, 0, 2, 0},
    {3, 0, 0, 0, 0, 0, RT48,  0, 0, 0, 0, 0, 3, 0},
    RES_CMD,
    {5, 0, 0, 0, 0, 0, RT136, 0, 0, 0, 0, 0, 5, 0},
    {6, 0, 0, 0, 0, 0, RT48,  0, 0, 0, 0, 0, 6, 0},
    {7, 0, 0, 0, 0, 0, RT48Busy, 0, 0, 0, 0, 0, 7, 0},
    {8, 0, 0, 0, 0, 0, RT48,  0, 0, 0, 0, 0, 8, 0},
    {9, 0, 0, 0, 0, 0, RT136, 0, 0, 0, 0, 0, 9, 0},
    RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD,
    {16, 0, 0, 0, 0, 0, RT48, 0, 0, 0, 0, 0, 16, 0},
    {17, 1, 0, 1, 0, 0, RT48, 0, 0, 0, 0, 0, 17, 0},
    {18, 1, 0, 1, 1, 0, RT48, 0, 0, 0, 0, 0, 18, 0},
    RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD,
    {24, 1, 0, 0, 0, 0, RT48, 0, 0, 0, 0, 0, 24, 0},
    RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD,
    RES_CMD,
    {41, 0, 1, 0, 0, 0, RT48, 0, 0, 0, 0, 0, 41, 0},
    RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD, RES_CMD,
    {51, 1, 1, 1, 0, 0, RT48, 0, 0, 0, 0, 0, 51, 0},
    RES_CMD, RES_CMD, RES_CMD,
    {55, 0, 0, 0, 0, 0, RT48, 0, 0, 0, 0, 0, 55, 0},
};

extern void timer_sleep(int ms);
extern uint32_t mailbox_clock_rate(uint32_t clock_id);
extern bool emmc_setup_clock(void);
extern bool switch_clock_rate(uint32_t base_clock, uint32_t target_rate);
extern void dbg_set_emmc(int ok, uint32_t lba);

static bool sd_wait_reg(volatile uint32_t *reg, uint32_t mask, bool set, uint32_t timeout) {
    while (timeout--) {
        if ((*reg & mask) ? set : !set) return true;
        delay(100);
    }
    return false;
}

static bool emmc_issue_command(emmc_cmd cmd, uint32_t arg, uint32_t timeout) {
    sd_device.last_command_value = TO_REG(&cmd);
    EMMC_BLKSIZECNT = sd_device.block_size | (sd_device.transfer_blocks << 16);
    EMMC_ARG1 = arg;
    EMMC_CMDTM = sd_device.last_command_value;
    delay(1000);
    while (timeout--) {
        if (EMMC_INTERRUPT & 0x8001) break;
        delay(100);
    }
    if (!(EMMC_INTERRUPT & 1)) return false;
    EMMC_INTERRUPT = 0xFFFF0001;
    if (cmd.response_type == RT136) {
        sd_device.last_response[0] = EMMC_RESP0; sd_device.last_response[1] = EMMC_RESP1;
        sd_device.last_response[2] = EMMC_RESP2; sd_device.last_response[3] = EMMC_RESP3;
    } else {
        sd_device.last_response[0] = EMMC_RESP0;
    }
    if (cmd.is_data) {
        uint32_t *data = (uint32_t *)sd_device.buffer;
        uint32_t wrIrpt = cmd.direction ? (1 << 5) : (1 << 4);
        for (int b = 0; b < sd_device.transfer_blocks; b++) {
            sd_wait_reg(&EMMC_INTERRUPT, wrIrpt | 0x8000, true, 2000);
            EMMC_INTERRUPT = wrIrpt | 0x8000;
            for (int i = 0; i < sd_device.block_size / 4; i++) {
                if (cmd.direction) *data++ = EMMC_DATA;
                else EMMC_DATA = *data++;
            }
        }
    }
    if (cmd.response_type == RT48Busy || cmd.is_data) {
        sd_wait_reg(&EMMC_INTERRUPT, 0x8002, true, 2000);
        EMMC_INTERRUPT = 0xFFFF0002;
    }
    return true;
}

bool emmc_command(uint32_t index, uint32_t arg, uint32_t timeout) {
    return emmc_issue_command(commands[index], arg, timeout);
}

static bool emmc_app_command(uint32_t index, uint32_t arg, uint32_t timeout) {
    if (emmc_issue_command(commands[55], sd_device.rca << 16, 2000))
        return emmc_issue_command(commands[index], arg, timeout);
    return false;
}

static bool emmc_card_reset() {
    EMMC_CONTROL1 = C1_RESET_HOST;
    if (!sd_wait_reg(&EMMC_CONTROL1, C1_RESET_ALL, false, 2000)) return false;
    if (!emmc_setup_clock()) return false;
    EMMC_IRPT_MASK = 0xFFFFFFFF; EMMC_IRPT_EN = 0xFFFFFFFF; EMMC_INTERRUPT = 0xFFFFFFFF;
    delay(20000);
    if (!emmc_command(0, 0, 2000)) return false;
    if (!emmc_command(8, 0x1AA, 2000) || (EMMC_RESP0 & 0xFFF) != 0x1AA) return false;
    while (!(EMMC_RESP0 & (1u << 31))) {
        if (!emmc_app_command(41, 0x40FF8000, 2000)) return false;
        delay(50000);
    }
    sd_device.sdhc = (EMMC_RESP0 >> 30) & 1;
    if (!emmc_command(2, 0, 2000)) return false;
    if (!emmc_command(3, 0, 2000)) return false;
    sd_device.rca = EMMC_RESP0 >> 16;
    if (!emmc_command(7, sd_device.rca << 16, 2000)) return false;
    sd_device.block_size = 512; sd_device.transfer_blocks = 1;
    switch_clock_rate(mailbox_clock_rate(1), SD_CLOCK_NORMAL);
    return true;
}

int emmc_init_card(void) {
    uart_puts("[emmc] starting initialization...\n");
    volatile uint32_t *f4 = (volatile uint32_t*)(MMIO_BASE + 0x200000 + 0x10);
    volatile uint32_t *f5 = (volatile uint32_t*)(MMIO_BASE + 0x200000 + 0x14);
    *f4 = (*f4 & ~(7 << 24 | 7 << 27)) | (7 << 24 | 7 << 27);
    *f5 = (*f5 & ~(7 << 0 | 7 << 3 | 7 << 6 | 7 << 9)) | (7 << 0 | 7 << 3 | 7 << 6 | 7 << 9);

    if (!emmc_card_reset()) return -1;
    
    static uint8_t mbr[512] __attribute__((aligned(16)));
    sd_device.buffer = mbr; sd_device.block_size = 512; sd_device.transfer_blocks = 1;
    if (emmc_command(17, 0, 2000) && mbr[510] == 0x55 && mbr[511] == 0xAA) {
        uint32_t b_lba = 0, b_sz = 0;
        for (int i=0; i<4; i++) {
            uint8_t *p = mbr + 446 + i*16;
            uint32_t lba = *(uint32_t*)(p+8), sz = *(uint32_t*)(p+12);
            if (sz > b_sz) { b_sz = sz; b_lba = lba; }
        }
        sd_diskfs_part_lba = b_lba;
        dbg_set_emmc(1, sd_diskfs_part_lba);
        uart_puts("[emmc] found part LBA="); uart_put_hex(sd_diskfs_part_lba); uart_puts("\n");
        return 0;
    }
    return -1;
}

int emmc_rw(uint64_t sector, void *buf, int write) {
    sd_device.buffer = (uint8_t*)buf; sd_device.block_size = 512; sd_device.transfer_blocks = 1;
    uint32_t real_lba = (uint32_t)sector + sd_diskfs_part_lba;
    return emmc_command(write ? 24 : 17, real_lba, 2000) ? 0 : -1;
}
