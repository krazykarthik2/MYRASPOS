#include "panic.h"
#include "uart.h"
#include <stdint.h>
#include <stddef.h>
#include "sched.h"
#include "irq.h"
#include "syscall.h"
#include "framebuffer.h"

extern int screen_w, screen_h;

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
extern void fb_put_text(const char *s, int x, int y, uint32_t color);


extern void rpi_gpu_flush(void);
extern void virtio_gpu_flush(void);
#ifdef REAL
    #define DBG_TEXT(y, msg, col) do { fb_draw_rect(0, y-500, screen_w, 20, 0xFF000000); fb_put_text(msg, 10, y-500, col); rpi_gpu_flush(); } while(0)
#else
    #define DBG_TEXT(y, msg, col) do { fb_draw_rect(0, y-500, screen_w, 20, 0xFF000000); fb_put_text(msg, 10, y-500, col); virtio_gpu_flush(); } while(0)
#endif
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

    DBG_TEXT(800,"\n[PANIC] EXCEPTION OCCURRED!\n",0xFFFFFFFF);
    uart_puts("\n[PANIC] EXCEPTION OCCURRED!\n");
    uart_puts("Type: "); print_hex((uintptr_t)type); uart_puts("\n");
    uart_puts("ESR:  "); print_hex((uintptr_t)esr);  uart_puts("\n");
    uart_puts("ELR:  "); print_hex((uintptr_t)elr);  uart_puts("\n");
    
    uint64_t far;
    asm("mrs %0, far_el1" : "=r"(far));
    uart_puts("FAR:  "); print_hex((uintptr_t)far);  uart_puts("\n");
    

    
    char buf_type[64] = "Type: 0x0000000000000000";
    char buf_esr[64]  = "ESR:  0x0000000000000000";
    char buf_elr[64]  = "ELR:  0x0000000000000000";
    char buf_far[64]  = "FAR:  0x0000000000000000";
    
    for (int i = 15; i >= 0; --i) {
        int v = (type >> (i * 4)) & 0xF;
        buf_type[8 + 15 - i] = (v < 10) ? ('0' + v) : ('A' + (v - 10));
        
        v = (esr >> (i * 4)) & 0xF;
        buf_esr[8 + 15 - i] = (v < 10) ? ('0' + v) : ('A' + (v - 10));
        
        v = (elr >> (i * 4)) & 0xF;
        buf_elr[8 + 15 - i] = (v < 10) ? ('0' + v) : ('A' + (v - 10));
        
        v = (far >> (i * 4)) & 0xF;
        buf_far[8 + 15 - i] = (v < 10) ? ('0' + v) : ('A' + (v - 10));
    }
    
    fb_put_text("PANIC: EXCEPTION OCCURRED!", 50, 100, 0xFF0000FF);
    fb_put_text(buf_type, 50, 120, 0xFF0000FF);
    fb_put_text(buf_esr, 50, 140, 0xFF0000FF);
    fb_put_text(buf_elr, 50, 160, 0xFF0000FF);
    fb_put_text(buf_far, 50, 180, 0xFF0000FF);
    
#ifdef REAL
    rpi_gpu_flush();
#else
    virtio_gpu_flush();
#endif

    panic_with_trace("Exception");
}
