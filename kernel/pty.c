#include "pty.h"
#include "kmalloc.h"
#include <string.h>

struct pty* pty_alloc(void) {
    struct pty *p = kmalloc(sizeof(struct pty));
    if (p) memset(p, 0, sizeof(*p));
    return p;
}

void pty_write_in(struct pty *p, char c) {
    int next = (p->in_h + 1) % 256;
    if (next != p->in_t) {
        p->in_buf[p->in_h] = c;
        p->in_h = next;
    }
}

char pty_read_in(struct pty *p) {
    if (p->in_h == p->in_t) return 0;
    char c = p->in_buf[p->in_t];
    p->in_t = (p->in_t + 1) % 256;
    return c;
}

void pty_write_out(struct pty *p, char c) {
    int next = (p->out_h + 1) % 4096;
    if (next != p->out_t) {
        p->out_buf[p->out_h] = c;
        p->out_h = next;
    }
}

char pty_read_out(struct pty *p) {
    if (p->out_h == p->out_t) return 0;
    char c = p->out_buf[p->out_t];
    p->out_t = (p->out_t + 1) % 4096;
    return c;
}

int pty_has_out(struct pty *p) {
    return p->out_h != p->out_t;
}
