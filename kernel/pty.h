#ifndef PTY_H
#define PTY_H

#include <stdint.h>

struct pty {
    char in_buf[256];
    int in_h, in_t;
    char out_buf[4096];
    int out_h, out_t;
};

struct pty* pty_alloc(void);
void pty_write_in(struct pty *p, char c);
char pty_read_in(struct pty *p);
void pty_write_out(struct pty *p, char c);
char pty_read_out(struct pty *p);
int pty_has_out(struct pty *p);

#endif
