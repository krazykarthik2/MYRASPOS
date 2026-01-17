#include "wm.h"
#include "pty.h"
#include "framebuffer.h"
#include "kmalloc.h"
#include "sched.h"
#include "input.h"
#include <string.h>
#include "terminal_app.h"

#define TERM_ROWS 24
#define TERM_COLS 80

struct terminal_app {
    struct pty *pty;
    char grid[TERM_ROWS][TERM_COLS];
    int cursor_x, cursor_y;
};


static struct terminal_app *g_term = NULL;

static void term_render_fn(struct window *win) {
    if (!g_term) return;
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            char ch = g_term->grid[r][c];
            if (ch > 0) {
                char s[2] = {ch, 0};
                fb_draw_text(win->x + 5 + c * 7, win->y + 25 + r * 10, s, 0x00FF00, 1);
            }
        }
    }
    /* Draw cursor */
    fb_draw_rect(win->x + 5 + g_term->cursor_x * 7, win->y + 25 + g_term->cursor_y * 10, 6, 9, 0x00AA00);
}

static uint8_t scan_to_ascii[] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

static void term_update_task(void *arg) {
    (void)arg;
    while (1) {
        if (g_term && pty_has_out(g_term->pty)) {
            char c = pty_read_out(g_term->pty);
            if (c == '\n') {
                g_term->cursor_x = 0;
                g_term->cursor_y++;
            } else if (c == '\r') {
                g_term->cursor_x = 0;
            } else if (c == '\b') {
                if (g_term->cursor_x > 0) g_term->cursor_x--;
                g_term->grid[g_term->cursor_y][g_term->cursor_x] = ' ';
            } else {
                if (g_term->cursor_x < TERM_COLS && g_term->cursor_y < TERM_ROWS) {
                    g_term->grid[g_term->cursor_y][g_term->cursor_x] = c;
                    g_term->cursor_x++;
                }
            }
            if (g_term->cursor_y >= TERM_ROWS) {
                /* Scroll */
                memmove(g_term->grid[0], g_term->grid[1], TERM_COLS * (TERM_ROWS - 1));
                memset(g_term->grid[TERM_ROWS - 1], ' ', TERM_COLS);
                g_term->cursor_y = TERM_ROWS - 1;
            }
        }
        
        /* Check keyboard and push to pty */
        struct input_event ev;
        while (input_pop_event(&ev)) {
            if (ev.type == INPUT_TYPE_KEY && ev.value == 1) { /* Pressed */
                if (ev.code < sizeof(scan_to_ascii)) {
                    char ch = scan_to_ascii[ev.code];
                    if (ch != 0) pty_write_in(g_term->pty, ch);
                }
            }
        }

        yield();
    }
}

extern void shell_main(void *arg);

void terminal_app_start(void) {
    g_term = kmalloc(sizeof(struct terminal_app));
    memset(g_term, 0, sizeof(*g_term));
    g_term->pty = pty_alloc();
    
    wm_create_window("Terminal", 50, 50, 600, 300, term_render_fn);
    
    task_create(term_update_task, NULL, "term_emulator");
    int shell_pid = task_create(shell_main, g_term->pty, "gui_shell");
    if (shell_pid > 0) task_set_tty(shell_pid, g_term->pty);
}
