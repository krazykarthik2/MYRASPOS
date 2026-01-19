#ifndef PTY_H
#define PTY_H

#include <stdint.h>

struct pty {
    char in_buf[512];
    int in_h, in_t;
    char out_buf[2048];
    int out_h, out_t;
    volatile int lock;
};

struct pty* pty_alloc(void);
void pty_write_in(struct pty *p, char c);
char pty_read_in(struct pty *p);
void pty_write_out(struct pty *p, char c);
char pty_read_out(struct pty *p);
int pty_has_out(struct pty *p);
int pty_has_in(struct pty *p);
void pty_free(struct pty *p);

#endif
