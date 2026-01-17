#include "timer.h"
#include "sched.h"
#include "uart.h"
#include <stdint.h>

/* Software-monotonic fallback timer.
   Using a simple tick counter advanced from the scheduler loop to
   avoid accessing platform MMIO during early init which can cause
   data aborts on some QEMU setups. This provides a reliable
   monotonic clock and sleep behavior for now. */

static volatile uint32_t last_ms = 0;

void timer_init(void) {
    last_ms = 0;
}

uint32_t timer_get_ms(void) {
    return last_ms;
}

void timer_sleep_ms(uint32_t ms) {
    uint32_t now = timer_get_ms();
    uint32_t wake = now + ms;
    /* block current task until wake (scheduler will wake it when tick advances) */
    task_block_current_until(wake);
}

/* Called from scheduler loop to advance the software tick by 1 ms. */
void timer_poll_and_advance(void) {
    last_ms++;
    scheduler_tick_advance(1);
}
