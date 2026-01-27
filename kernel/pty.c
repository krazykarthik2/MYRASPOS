#include "pty.h"
#include "kmalloc.h"
#include <string.h>
#include "irq.h"
#include "uart.h"



struct pty* pty_alloc(void) {
    struct pty *p = kmalloc(sizeof(struct pty));
    if (p) {
        memset(p, 0, sizeof(*p));
        p->lock = 0;
    } else {
        uart_puts("[pty] ERROR: pty_alloc failed (kmalloc returned NULL)\n");
    }
    return p;
}

void pty_write_in(struct pty *p, char c) {
    if (!p) return;
    // uart_puts("[pty] write_in '"); uart_putc(c); uart_puts("'\n");
    unsigned long flags = irq_save();
    int next = (p->in_h + 1) % 512;
    if (next != p->in_t) {
        p->in_buf[p->in_h] = c;
        p->in_h = next;
    } 
    irq_restore(flags);
}

char pty_read_in(struct pty *p) {
    if (!p) return 0;
    unsigned long flags = irq_save();
    if (p->in_h == p->in_t) {
        irq_restore(flags);
        return 0;
    }
    char c = p->in_buf[p->in_t];
    p->in_t = (p->in_t + 1) % 512;
    irq_restore(flags);
    // uart_puts("[pty] read_in '"); uart_putc(c); uart_puts("'\n");
    return c;
}

void pty_write_out(struct pty *p, char c) {
    if (!p) return;
    unsigned long flags = irq_save();
    int next = (p->out_h + 1) % 2048;
    if (next != p->out_t) {
        p->out_buf[p->out_h] = c;
        p->out_h = next;
    }
    irq_restore(flags);
}

char pty_read_out(struct pty *p) {
    if (!p) return 0;
    unsigned long flags = irq_save();
    if (p->out_h == p->out_t) {
        irq_restore(flags);
        return 0;
    }
    char c = p->out_buf[p->out_t];
    p->out_t = (p->out_t + 1) % 2048;
    irq_restore(flags);
    return c;
}

int pty_has_out(struct pty *p) {
    if (!p) return 0;
    unsigned long flags = irq_save();
    int has = (p->out_h != p->out_t);
    irq_restore(flags);
    return has;
}

int pty_has_in(struct pty *p) {
    if (!p) return 0;
    unsigned long flags = irq_save();
    int has = (p->in_h != p->in_t);
    irq_restore(flags);
    return has;
}

void pty_free(struct pty *p) {
    if (p) kfree(p);
}

#include "sched.h"

int pty_getline(struct pty *p, char *buf, int max_len) {
    if (!p || !buf || max_len <= 0) return 0;
    
    int i = 0;
    while (i < max_len - 1) {
        /* Wait for input */
        while (!pty_has_in(p)) {
            /* Block for 20ms */
            // uart_puts("z"); 
            task_block_current_until(scheduler_get_tick() + 20);
        }
        
        char c = pty_read_in(p);
        
        /* Handle line editing */
        if (c == '\r' || c == '\n') {
            pty_write_out(p, '\n');
            break;
        }
        
        if (c == '\b' || c == 127) {
            if (i > 0) {
                i--;
                pty_write_out(p, '\b'); 
                pty_write_out(p, ' '); 
                pty_write_out(p, '\b');
            }
            continue;
        }
        
        /* Echo and store */
        pty_write_out(p, c);
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}
