#include "calculator_app.h"
#include "wm.h"
#include "framebuffer.h"
#include "kmalloc.h"
#include "lib.h"
#include "input.h"
#include "sched.h"
#include <string.h>

struct calc_state {
    struct window *win;
    char top_text[64];
    char bottom_text[64];
    long accumulator;
    char last_op; /* +, -, *, / */
    int clear_bottom; /* 1 if next digit replaces bottom_text */
    int has_result;   /* 1 if bottom_text is a final result */
};

static struct calc_state *g_calc = NULL;

static void calc_on_close(struct window *win) {
    (void)win;
    if (g_calc) {
        /* let the task clean up or just free here if no task? 
           we will use a task for input polling */
        g_calc = NULL;
    }
}

static void calc_draw(struct window *win) {
    if (!g_calc) return;
    
    /* Display Background (Sleeker dark grey) */
    wm_draw_rect(win, 8, 8, win->w - 16, 60, 0x1A1A1A);
    
    /* Top text (Expression) - Small, dimmed gray */
    int top_len = (int)strlen(g_calc->top_text);
    int top_x = (win->w - 20) - (top_len * 7); 
    if (top_x < 12) top_x = 12;
    wm_draw_text(win, top_x, 15, g_calc->top_text, 0x888888, 1);
    
    /* Bottom text (Current Entry/Result) - Large, bright green or white */
    int bot_len = (int)strlen(g_calc->bottom_text);
    int bot_x = (win->w - 20) - (bot_len * 14); 
    if (bot_x < 12) bot_x = 12;
    wm_draw_text(win, bot_x, 35, g_calc->bottom_text, (g_calc->has_result ? 0x55FF55 : 0xFFFFFF), 2);

    /* Grid of buttons */
    const char *labels[] = {
        "C", "±", "%", "/",
        "7", "8", "9", "*",
        "4", "5", "6", "-",
        "1", "2", "3", "+",
        "0", " ", "=", "" 
    };
    /* Adjusted layout: 4x5 or 4x4. Let's do 4x5 to include more symbols. */
    int grid_rows = 5;
    int grid_cols = 4;
    int btn_w = (win->w - 40) / grid_cols;
    int btn_h = (win->h - 110) / grid_rows;
    
    for (int i = 0; i < grid_rows * grid_cols; i++) {
        if (labels[i][0] == '\0' || labels[i][0] == ' ') continue;
        
        int r = i / grid_cols;
        int c = i % grid_cols;
        int bx = 12 + c * (btn_w + 2);
        int by = 80 + r * (btn_h + 2);
        
        /* Special colors for operators */
        uint32_t color = 0x333333;
        const char *l = labels[i];
        if (l[0] == '=' || l[0] == '+' || l[0] == '-' || l[0] == '*' || l[0] == '/') color = 0xFF9500; // Orange
        else if (l[0] == 'C' || l[0] == '%' || strcmp(l, "±") == 0) color = 0xA5A5A5; // Light Gray
        
        wm_draw_rect(win, bx, by, btn_w, btn_h, color);
        
        uint32_t text_color = (color == 0xA5A5A5) ? 0x000000 : 0xFFFFFF;
        wm_draw_text(win, bx + btn_w/2 - 7, by + btn_h/2 - 7, labels[i], text_color, 2);
    }
}

static void update_display(long val) {
    char buf[32];
    int i = 0;
    int neg = 0;
    long v = val;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) { buf[i++] = '0'; }
    else {
        while (v > 0 && i < 30) {
            buf[i++] = (v % 10) + '0';
            v /= 10;
        }
    }
    if (neg) buf[i++] = '-';
    
    int j = 0;
    while (i > 0) g_calc->bottom_text[j++] = buf[--i];
    g_calc->bottom_text[j] = '\0';
}

static void do_op(void) {
    long cur = atoi(g_calc->bottom_text);
    if (g_calc->last_op == 0) {
        g_calc->accumulator = cur;
    } else {
        switch(g_calc->last_op) {
            case '+': g_calc->accumulator += cur; break;
            case '-': g_calc->accumulator -= cur; break;
            case '*': g_calc->accumulator *= cur; break;
            case '/': if (cur != 0) g_calc->accumulator /= cur; break;
        }
    }
}

static void handle_input(char key) {
    if (key >= '0' && key <= '9') {
        if (g_calc->clear_bottom) {
            strcpy(g_calc->bottom_text, "");
            g_calc->clear_bottom = 0;
        }
        if (strcmp(g_calc->bottom_text, "0") == 0) g_calc->bottom_text[0] = '\0';
        size_t len = strlen(g_calc->bottom_text);
        if (len < 15) {
            g_calc->bottom_text[len] = key;
            g_calc->bottom_text[len+1] = '\0';
        }
        g_calc->has_result = 0;
    } else if (key == 'C' || key == 'c' || key == 27) { /* 27 is ESC */
        strcpy(g_calc->top_text, "");
        strcpy(g_calc->bottom_text, "0");
        g_calc->accumulator = 0;
        g_calc->last_op = 0;
        g_calc->clear_bottom = 1;
        g_calc->has_result = 0;
    } else if (key == '\b') { /* Backspace */
        size_t len = strlen(g_calc->bottom_text);
        if (len > 0 && !g_calc->clear_bottom) {
            g_calc->bottom_text[len-1] = '\0';
            if (strlen(g_calc->bottom_text) == 0) strcpy(g_calc->bottom_text, "0");
        }
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
        do_op();
        g_calc->last_op = key;
        g_calc->clear_bottom = 1;
        /* Update top display: "Accumulator Op" */
        update_display(g_calc->accumulator);
        strcpy(g_calc->top_text, g_calc->bottom_text);
        size_t tl = strlen(g_calc->top_text);
        g_calc->top_text[tl] = ' ';
        g_calc->top_text[tl+1] = key;
        g_calc->top_text[tl+2] = '\0';
        g_calc->has_result = 0;
    } else if (key == '=' || key == '\n') {
        /* If we have an op and a new value in bottom, complete it */
        if (g_calc->last_op != 0) {
            /* For the top display: "Op1 Op Op2 =" */
            char op2_str[32];
            strcpy(op2_str, g_calc->bottom_text);
            do_op();
            
            /* Build top text */
            /* We don't have sprintf, so we concatenation */
            /* accumulator is now the result. Let's rebuild top string. */
            /* Actually it's easier to just show the full result on bottom and clear top. */
            strcpy(g_calc->top_text, "");
        }
        g_calc->last_op = 0;
        g_calc->clear_bottom = 1;
        g_calc->has_result = 1;
        update_display(g_calc->accumulator);
    } else if (key == '%') {
        long cur = atoi(g_calc->bottom_text);
        update_display(cur / 100);
        g_calc->has_result = 1;
        g_calc->clear_bottom = 1;
    } else if (key == 's') { /* Sign Toggle */
        if (strcmp(g_calc->bottom_text, "0") != 0) {
            if (g_calc->bottom_text[0] == '-') {
                size_t len = strlen(g_calc->bottom_text);
                memmove(g_calc->bottom_text, g_calc->bottom_text + 1, len);
            } else {
                size_t len = strlen(g_calc->bottom_text);
                memmove(g_calc->bottom_text + 1, g_calc->bottom_text, len + 1);
                g_calc->bottom_text[0] = '-';
            }
        }
    }
}

static void calc_task(struct calc_state *st) {
    int last_btn = 0;
    int shift_state = 0;
    while (g_calc && g_calc == st) {
        /* Keyboard */
        if (wm_is_focused(g_calc->win)) {
            struct wm_input_event ev;
            while (wm_pop_key_event(g_calc->win, &ev)) {
                if (ev.type == INPUT_TYPE_KEY) {
                    /* Track Shift */
                    if (ev.code == 0x2A || ev.code == 0x36) {
                        shift_state = ev.value;
                        continue;
                    }
                    
                    if (ev.value == 1) { /* Key Down */
                        char c = 0;
                        /* 1. Main Number Row */
                        if (ev.code >= 0x02 && ev.code <= 0x0B) {
                            static const char keys[] = "1234567890";
                            static const char shifts[] = "!@#$%^&*()";
                            c = shift_state ? shifts[ev.code - 0x02] : keys[ev.code - 0x02];
                        }
                        /* 2. Main Operators */
                        else if (ev.code == 0x0C) c = shift_state ? '_' : '-'; // -/_
                        else if (ev.code == 0x0D) c = shift_state ? '+' : '='; // =/+
                        else if (ev.code == 0x35) c = shift_state ? '?' : '/'; // //?
                        else if (ev.code == 0x09) { if (shift_state) c = '*'; } // Shift+8
                        else if (ev.code == 0x06) { if (shift_state) c = '%'; } // Shift+5
                        
                        /* 3. Numpad */
                        else if (ev.code == 0x47) c = '7';
                        else if (ev.code == 0x48) c = '8';
                        else if (ev.code == 0x49) c = '9';
                        else if (ev.code == 0x37) c = '*'; // KP *
                        else if (ev.code == 0x4B) c = '4';
                        else if (ev.code == 0x4C) c = '5';
                        else if (ev.code == 0x4D) c = '6';
                        else if (ev.code == 0x4A) c = '-'; // KP -
                        else if (ev.code == 0x4F) c = '1';
                        else if (ev.code == 0x50) c = '2';
                        else if (ev.code == 0x51) c = '3';
                        else if (ev.code == 0x4E) c = '+'; // KP +
                        else if (ev.code == 0x52) c = '0';
                        
                        /* 4. Controls */
                        else if (ev.code == 0x01) c = 27;   /* ESC */
                        else if (ev.code == 0x0E) c = '\b'; /* Backspace */
                        else if (ev.code == 0x1C || ev.code == 0x60) c = '=';  /* Enter/KPEnter -> Equals */
                        
                        if (c != 0) {
                            handle_input(c);
                            wm_request_render(g_calc->win);
                        }
                    }
                }
            }
        }
        
        /* Mouse */
        int mx, my, mbtn;
        wm_get_mouse_state(&mx, &my, &mbtn);
        if (mbtn && !last_btn) {
            int wx = g_calc->win->x;
            int wy = g_calc->win->y;
            if (mx > wx && mx < wx + g_calc->win->w && my > wy && my < wy + g_calc->win->h) {
                int grid_cols = 4;
                int grid_rows = 5;
                int btn_w = (g_calc->win->w - 40) / grid_cols;
                int btn_h = (g_calc->win->h - 110) / grid_rows;
                
                for (int i = 0; i < grid_rows * grid_cols; i++) {
                    int r = i / grid_cols;
                    int c = i % grid_cols;
                    int bx = wx + 15 + c * (btn_w + 2);
                    int by = wy + 100 + r * (btn_h + 2);
                    
                    if (mx >= bx && mx <= bx + btn_w && my >= by && my <= by + btn_h) {
                        const char *labels[] = {
                            "C", "±", "%", "/",
                            "7", "8", "9", "*",
                            "4", "5", "6", "-",
                            "1", "2", "3", "+",
                            "0", " ", "=", "" 
                        };
                        const char *selection = labels[i];
                        if (i == 1) handle_input('s'); /* ± */
                        else if (i == 16) handle_input('0');
                        else if (i == 17) handle_input('.');
                        else if (selection[0] != ' ' && selection[0] != '\0') {
                            handle_input(selection[0]);
                        }
                        wm_request_render(g_calc->win);
                        break;
                    }
                }
            }
        }
        last_btn = mbtn;
        
        yield();
    }
    
    if (g_calc == st) g_calc = NULL;
    kfree(st);
    task_set_fn_null(task_current_id());
}

/* shim for task_create */
static void calc_task_entry(void *arg) {
    calc_task((struct calc_state*)arg);
}

void calculator_app_start(void) {
    if (g_calc) return;
    
    struct calc_state *st = kmalloc(sizeof(struct calc_state));
    memset(st, 0, sizeof(*st));
    strcpy(st->top_text, "");
    strcpy(st->bottom_text, "0");
    st->clear_bottom = 1;
    
    st->win = wm_create_window("Calculator", 200, 200, 300, 420, calc_draw);
    st->win->on_close = calc_on_close;
    
    g_calc = st;
    int calc_task_id = task_create(calc_task_entry, st, "calculator");
    
    /* CRITICAL: Reparent to init (1) so cascade kill from launcher doesn't kill us */
    task_set_parent(calc_task_id, 1);
}
