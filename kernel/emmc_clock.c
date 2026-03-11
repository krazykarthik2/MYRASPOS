#include "peripherals/emmc.h"
#include "rpi_fx.h"
#include <stdint.h>
#include <stdbool.h>

extern uint32_t mailbox_clock_rate(uint32_t clock_id);

static uint32_t get_clock_divider(uint32_t base_clock, uint32_t target_rate) {
    uint32_t target_div = target_rate <= base_clock ? (base_clock / target_rate) : 1;
    if (base_clock % target_rate) target_div = 0;
    int div = -1;
    for (int fb = 31; fb >= 0; fb--) {
        if (target_div & (1 << fb)) {
            div = fb;
            target_div &= ~(1 << fb);
            if (target_div) div++;
            break;
        }
    }
    if (div == -1 || div >= 32) div = 31;
    if (div != 0) div = (1 << (div - 1));
    if (div >= 0x400) div = 0x3FF;
    return ((div & 0xff) << 8) | (((div >> 8) & 0x3) << 6);
}

static bool sd_wait_reg(volatile uint32_t *reg, uint32_t mask, bool set, uint32_t timeout) {
    while (timeout--) {
        if ((*reg & mask) ? set : !set) return true;
        delay(100);
    }
    return false;
}

bool emmc_setup_clock() {
    EMMC_CONTROL2 = 0;
    uint32_t rate = mailbox_clock_rate(1);
    uint32_t n = EMMC_CONTROL1 & ~(0xf << 16);
    n |= C1_CLK_INTLEN | get_clock_divider(rate, SD_CLOCK_ID) | (11 << 16);
    EMMC_CONTROL1 = n;
    if (!sd_wait_reg(&EMMC_CONTROL1, C1_CLK_STABLE, true, 2000)) return false;
    delay(3000);
    EMMC_CONTROL1 |= C1_CLK_EN;
    delay(3000);
    return true;
}

bool switch_clock_rate(uint32_t base_clock, uint32_t target_rate) {
    uint32_t divider = get_clock_divider(base_clock, target_rate);
    while (EMMC_STATUS & (SR_CMD_INHIBIT | SR_DAT_INHIBIT)) delay(100);
    EMMC_CONTROL1 &= ~C1_CLK_EN;
    delay(300);
    EMMC_CONTROL1 = (EMMC_CONTROL1 & ~0xFFE0) | divider;
    delay(300);
    EMMC_CONTROL1 |= C1_CLK_EN;
    delay(300);
    return true;
}
