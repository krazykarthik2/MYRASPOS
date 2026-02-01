#include "panic.h"
#include "uart.h"
#include <stdint.h>
#include <stddef.h>
#include "sched.h"
#include "irq.h"
#include "syscall.h"

static void print_hex(uintptr_t x) {
    char buf[2 + sizeof(uintptr_t) * 2 + 1];
    int pos = 0;
    buf[pos++] = '0'; buf[pos++] = 'x';
    for (int i = (int)(sizeof(uintptr_t) * 2 - 1); i >= 0; --i) {
        int v = (x >> (i * 4)) & 0xF;
        buf[pos++] = (v < 10) ? ('0' + v) : ('A' + (v - 10));
    }
    
    buf[pos] = '\0';
    uart_puts(buf);
}

void panic_with_trace(const char *msg) {
    uart_puts("\n[PANIC] ");
    uart_puts(msg);
    uart_puts("\nBacktrace:\n");

    // Walk frame pointers
    void **frame = (void **)__builtin_frame_address(0);
    for (int i = 0; i < 16 && frame; ++i) {
        void *ret = *(frame + 1);
        if (!ret) break;
        uart_puts("  ");
        print_hex((uintptr_t)ret);
        uart_puts("\n");
        frame = (void **)(*frame);
    }

    uart_puts("System halted.\n");
    
    uart_puts("Task: ");
    int tid = task_current_id();
    print_hex((uintptr_t)tid);
    uart_puts("\n");

    while (1) ;
}

extern void irq_entry_c(void);

void exception_c_handler(int type, uint64_t esr, uint64_t elr, struct pt_regs *regs) {
    uint32_t ec = (esr >> 26);

    if (type == 1 || type == 5 || type == 9 || type == 13) {
        irq_entry_c();
        /* Preemption handled inside irq_entry_c via scheduler_request_preempt 
           and return path via ret_from_exception triggers schedule() check? 
           Actually vectors.S kernel_exit doesn't check preemption. 
           We rely on timer tick to set a flag? 
           Currently irq_entry_c calls scheduler_request_preempt. 
           But we need a check on exit! 
           Existing code didn't have it in vectors.S. 
           (It was: handle_c -> exception_c_handler -> irq_entry_c -> request_preempt).
           But handle_c calls kernel_exit immediately. 
           So Preemption never happens? 
           Wait, irq_entry_c does NOT context switch directly.
           Preemption logic usually requires checking 'need_resched' in assembly before restoring regs.
           That is separate issue. For now, stick to original flow.
        */
        return;
    }

    if (ec == 0x15) { /* SVC from AArch64 */
        uint32_t sys_num = (uint32_t)regs->regs[8];

        /* Call syscall handler with up to 3 args */
        regs->regs[0] = syscall_handle(sys_num, regs->regs[0], regs->regs[1], regs->regs[2]);
        
        /* Advance PC past SVC instruction */
        regs->elr += 4;
        
        return;
    }

    uart_puts("\n[PANIC] EXCEPTION OCCURRED!\n");
    uart_puts("Type: "); print_hex((uintptr_t)type); uart_puts("\n");
    uart_puts("ESR:  "); print_hex((uintptr_t)esr);  uart_puts("\n");
    uart_puts("ELR:  "); print_hex((uintptr_t)elr);  uart_puts("\n");
    
    uint64_t far;
    asm("mrs %0, far_el1" : "=r"(far));
    uart_puts("FAR:  "); print_hex((uintptr_t)far);  uart_puts("\n");
    
    panic_with_trace("Exception");
}
