#include "pty.h"
#include "kmalloc.h"
#include <string.h>
#include "uart.h"

static unsigned long pty_lock(struct pty *p) {
    (void)p;
    unsigned long flags;
    /* Save DAIF and disable IRQ to ensure atomicity on single-core */
    __asm__ volatile(
        "mrs %0, daif\n"
        "msr daifset, #2\n"
        : "=r" (flags)
        :
        : "memory"
    );
    return flags;
}

static void pty_unlock(struct pty *p, unsigned long flags) {
    (void)p;
    __asm__ volatile("dmb sy" ::: "memory");
    /* Restore DAIF */
    __asm__ volatile("msr daif, %0" :: "r" (flags) : "memory");
}

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
    unsigned long flags = pty_lock(p);
    int next = (p->in_h + 1) % 512;
    if (next != p->in_t) {
        p->in_buf[p->in_h] = c;
        p->in_h = next;
    } 
    pty_unlock(p, flags);
}

char pty_read_in(struct pty *p) {
    if (!p) return 0;
    unsigned long flags = pty_lock(p);
    if (p->in_h == p->in_t) {
        pty_unlock(p, flags);
        return 0;
    }
    char c = p->in_buf[p->in_t];
    p->in_t = (p->in_t + 1) % 512;
    pty_unlock(p, flags);
    return c;
}

void pty_write_out(struct pty *p, char c) {
    if (!p) return;
    unsigned long flags = pty_lock(p);
    int next = (p->out_h + 1) % 2048;
    if (next != p->out_t) {
        p->out_buf[p->out_h] = c;
        p->out_h = next;
    }
    pty_unlock(p, flags);
}

char pty_read_out(struct pty *p) {
    if (!p) return 0;
    unsigned long flags = pty_lock(p);
    if (p->out_h == p->out_t) {
        pty_unlock(p, flags);
        return 0;
    }
    char c = p->out_buf[p->out_t];
    p->out_t = (p->out_t + 1) % 2048;
    pty_unlock(p, flags);
    return c;
}

int pty_has_out(struct pty *p) {
    if (!p) return 0;
    unsigned long flags = pty_lock(p);
    int has = (p->out_h != p->out_t);
    pty_unlock(p, flags);
    return has;
}

int pty_has_in(struct pty *p) {
    if (!p) return 0;
    unsigned long flags = pty_lock(p);
    int has = (p->in_h != p->in_t);
    // if (has) { uart_puts("[pty] has_in YES\n"); }
    pty_unlock(p, flags);
    return has;
}

void pty_free(struct pty *p) {
    if (p) kfree(p);
}
