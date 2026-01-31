#include "wm.h"
#include "pty.h"
#include "uart.h"
#include "framebuffer.h"
#include "kmalloc.h"
#include "sched.h"
#include "input.h"
#include <string.h>
#include "lib.h"
#include "timer.h"
#include "terminal_app.h"

#define TERM_ROWS 24
#define TERM_COLS 80

struct terminal_app {
    struct pty *pty;
    char grid[TERM_ROWS][TERM_COLS];
    int cursor_x, cursor_y;
    int shell_pid;
    struct window *win;
    int cursor_visible;
    uint32_t last_blink;
};


static struct terminal_app *g_term = NULL;

static void terminal_on_close(struct window *win) {
    (void)win;
    if (g_term) {
        /* Kill shell if it's still running */
        if (g_term->shell_pid > 0) task_kill(g_term->shell_pid);
        /* Signal task to exit and kfree */
        g_term = NULL;
    }
}

static void term_render_fn(struct window *win) {
    if (!g_term) return;
    
    /* Draw background (black) */
    /* (WM already does this, but we can do it here for safety or let WM handle it) */

    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            char ch = g_term->grid[r][c];
            if (ch > 32 && ch <= 126) {
                char s[2] = {ch, 0};
                wm_draw_text(win, 5 + c * 7, 5 + r * 10, s, 0x00FF00, 1);
            }
        }
    }
    
    /* Blink cursor every 500ms */
    uint32_t now = timer_get_ms();
    if (now - g_term->last_blink > 500) {
        g_term->cursor_visible = !g_term->cursor_visible;
        g_term->last_blink = now;
        wm_request_render(win);
    }

    if (g_term->cursor_visible) {
        wm_draw_rect(win, 5 + g_term->cursor_x * 7, 5 + g_term->cursor_y * 10, 6, 9, 0x00AA00);
    }
}



static void term_update_task(void *arg) {
    (void)arg;
    struct terminal_app *my_term = g_term;
    
    char input_buf[128];
    int input_buf_idx = 0;

    while (1) {
        if (!g_term) break;
        
        /* Use local var to avoid race if g_term cleared mid-loop */
        struct terminal_app *t = g_term;
        if (!t) break;

        /* Check shell death */
        if (t->shell_pid > 0 && !task_exists(t->shell_pid)) {
             struct window *w = t->win;
             g_term = NULL;
             wm_close_window(w);
             break;
        }

        /* Input Processing */
        struct wm_input_event ev;
        while (wm_pop_key_event(t->win, &ev)) {
             /* Input is handled by WM TTY forwarding now. 
                We just drain the queue to keep it from filling up. 
             */
        }

        int count = 0;
        int active = 0;
        while (g_term && pty_has_out(t->pty)) {
             active = 1;
             char c = pty_read_out(t->pty);

             /* ANSI Parser State Machine */
             /* State 0: Normal, 1: ESC, 2: '[' (CSI), 3: Params */
             static int term_state = 0;
             static char state_buf[16];
             static int state_idx = 0;

             if (term_state == 0) {
                 if (c == '\x1b') {
                     term_state = 1;
                 } else if (c == '\n') { 
                     t->cursor_x = 0; t->cursor_y++; 
                 } else if (c == '\r') { 
                     t->cursor_x = 0; 
                 } else if (c == '\b') { 
                     if (t->cursor_x > 0) t->cursor_x--; 
                     t->grid[t->cursor_y][t->cursor_x] = ' '; 
                 } else {
                     if (t->cursor_x < TERM_COLS && t->cursor_y < TERM_ROWS) {
                         t->grid[t->cursor_y][t->cursor_x] = c;
                         t->cursor_x++;
                     }
                 }
             } else if (term_state == 1) {
                 if (c == '[') {
                     term_state = 2;
                     state_idx = 0;
                 } else {
                     term_state = 0; /* invalid, drop */
                 }
             } else if (term_state == 2) {
                 if (c >= '0' && c <= '9') {
                     if (state_idx < 15) state_buf[state_idx++] = c;
                 } else if (c == ';') {
                     if (state_idx < 15) state_buf[state_idx++] = c;
                 } else {
                     /* Command char */
                     state_buf[state_idx] = '\0';
                     if (strcmp(state_buf, "2") == 0) {
                         /* 2J = clear screen */
                         memset(t->grid, ' ', sizeof(t->grid));
                         t->cursor_x = 0; t->cursor_y = 0;
                     } else if (strcmp(state_buf, "H") == 0) {
                         /* Home cursor (H or f) - ignoring params for now, assume 0,0 */
                         t->cursor_x = 0; t->cursor_y = 0;
                     }
                     term_state = 0;
                 }
             }

             /* Scroll check */
             if (t->cursor_y >= TERM_ROWS) {
                 memmove(t->grid[0], t->grid[1], TERM_COLS * (TERM_ROWS - 1));
                 memset(t->grid[TERM_ROWS - 1], ' ', TERM_COLS);
                 t->cursor_y = TERM_ROWS - 1;
             }
             t->cursor_visible = 1;
             t->last_blink = timer_get_ms();
             wm_request_render(t->win);

             if (++count > 64) { count = 0; yield(); }
        }
        if (!active) yield();
    }
    
    /* Cleanup */
    if (my_term) {
        if (my_term->shell_pid > 0 && task_exists(my_term->shell_pid)) task_kill(my_term->shell_pid);
        pty_free(my_term->pty);
        kfree(my_term);
    }
    task_set_fn_null(task_current_id());
}

extern void shell_main(void *arg);

void terminal_app_start(void) {
    uart_puts("[terminal] START - checking task list...\n");
    
    g_term = kmalloc(sizeof(struct terminal_app));
    uart_puts("[terminal] g_term allocated at: "); uart_put_hex((uintptr_t)g_term); uart_puts("\n");
    
    memset(g_term, 0, sizeof(*g_term));
    uart_puts("[terminal] g_term memset done\n");
    
    g_term->pty = pty_alloc();
    uart_puts("[terminal] pty alloc: "); uart_put_hex((uintptr_t)g_term->pty); uart_puts("\n");

    g_term->cursor_visible = 1;
    g_term->last_blink = timer_get_ms();
    
    g_term->win = wm_create_window("Terminal", 50, 50, 600, 300, term_render_fn);
    uart_puts("[terminal] window allocated at: "); uart_put_hex((uintptr_t)g_term->win); uart_puts("\n");
    
    g_term->win->on_close = terminal_on_close;
    g_term->win->tty = g_term->pty; /* Redirect input handled by WM */
    // g_term->win->tty = NULL;
    
    uart_puts("[terminal] About to create task. Checking task list...\n");
    
    int term_task_id = task_create(term_update_task, NULL, "term_emulator");
    
    uart_puts("[terminal] spawning shell with pty arg: "); uart_put_hex((uintptr_t)g_term->pty); uart_puts("\n");
    g_term->shell_pid = task_create(shell_main, g_term->pty, "gui_shell");
    if (g_term->shell_pid > 0) task_set_tty(g_term->shell_pid, g_term->pty);
    
    /* CRITICAL: Reparent to init (1) so cascade kill from launcher doesn't kill us */
    task_set_parent(term_task_id, 1);
    if (g_term->shell_pid > 0) task_set_parent(g_term->shell_pid, 1);
}
