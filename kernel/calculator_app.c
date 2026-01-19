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
    char display[32];
    int disp_len;
    long accumulator;
    long current_val;
    char op; /* +, -, *, / */
    int new_entry; /* 1 if next digit clears display */
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
    
    /* Display Background */
    fb_draw_rect(win->x + 20, win->y + 30, win->w - 40, 40, 0x222222);
    fb_draw_rect_outline(win->x + 20, win->y + 30, win->w - 40, 40, 0xAAAAAA, 1);
    
    /* Display Text (Right aligned roughly) */
    int text_len = strlen(g_calc->display);
    int text_x = win->x + win->w - 30 - (text_len * 10); /* approx width */
    if (text_x < win->x + 25) text_x = win->x + 25;
    fb_draw_text(text_x, win->y + 40, g_calc->display, 0x00FF00, 2);

    /* Grid of buttons */
    /* 
       7 8 9 /
       4 5 6 *
       1 2 3 -
       C 0 = +
    */
    const char *labels[] = {
        "7", "8", "9", "/",
        "4", "5", "6", "*",
        "1", "2", "3", "-",
        "C", "0", "=", "+"
    };
    
    int btn_w = (win->w - 60) / 4;
    int btn_h = (win->h - 100) / 4;
    
    for (int i = 0; i < 16; i++) {
        int r = i / 4;
        int c = i % 4;
        int bx = win->x + 20 + c * (btn_w + 5);
        int by = win->y + 80 + r * (btn_h + 5);
        
        fb_draw_rect(bx, by, btn_w, btn_h, 0x444444);
        fb_draw_rect_outline(bx, by, btn_w, btn_h, 0xFFFFFF, 1);
        
        fb_draw_text(bx + btn_w/2 - 5, by + btn_h/2 - 7, labels[i], 0xFFFFFF, 2);
    }
}

static void do_op(void) {
    long oval = 0;
    int i = 0;
    /* parse current display */
    /* simple header atoi */
    long val = 0;
    int sign = 1;
    char *s = g_calc->display;
    if (*s == '-') { sign = -1; s++; }
    while (*s) { val = val * 10 + (*s - '0'); s++; }
    val *= sign;
    
    if (g_calc->op == 0) {
        g_calc->accumulator = val;
    } else {
        switch(g_calc->op) {
            case '+': g_calc->accumulator += val; break;
            case '-': g_calc->accumulator -= val; break;
            case '*': g_calc->accumulator *= val; break;
            case '/': if (val != 0) g_calc->accumulator /= val; break;
        }
    }
}

static void update_display(long val) {
    if (val == 0) {
        strcpy(g_calc->display, "0");
        g_calc->disp_len = 1;
        return;
    }
    char buf[32];
    int i = 0;
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    if (val == 0) { buf[i++] = '0'; }
    else {
        while (val > 0) {
            buf[i++] = (val % 10) + '0';
            val /= 10;
        }
    }
    if (neg) buf[i++] = '-';
    
    /* reverse copy to display */
    int j = 0;
    while (i > 0) g_calc->display[j++] = buf[--i];
    g_calc->display[j] = '\0';
    g_calc->disp_len = j;
}

static void handle_input(char key) {
    if (key >= '0' && key <= '9') {
        if (g_calc->new_entry) {
            strcpy(g_calc->display, "");
            g_calc->disp_len = 0;
            g_calc->new_entry = 0;
        }
        if (strcmp(g_calc->display, "0") == 0) g_calc->disp_len = 0; /* replace 0 */
        if (g_calc->disp_len < 12) {
            g_calc->display[g_calc->disp_len++] = key;
            g_calc->display[g_calc->disp_len] = '\0';
        }
    } else if (key == 'C' || key == 'c') {
        strcpy(g_calc->display, "0");
        g_calc->disp_len = 1;
        g_calc->accumulator = 0;
        g_calc->op = 0;
        g_calc->new_entry = 1;
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
        do_op();
        g_calc->op = key;
        g_calc->new_entry = 1;
        update_display(g_calc->accumulator); /* Show running total */
        /* But we want next input to be fresh, kept in display until typing starts? 
           Yes, new_entry=1 handles that. */
    } else if (key == '=' || key == '\n') {
        do_op();
        g_calc->op = 0;
        g_calc->new_entry = 1;
        update_display(g_calc->accumulator);
    }
}

static void calc_task(struct calc_state *st) {
    int last_btn = 0;
    while (g_calc && g_calc == st) {
        /* Keyboard */
        if (wm_is_focused(g_calc->win)) {
            struct wm_input_event ev;
            while (wm_pop_key_event(g_calc->win, &ev)) {
                if (ev.type == INPUT_TYPE_KEY && ev.value == 1) {
                    char c = 0;
                    /* simple map */
                    static const char map[] = {0,27,'1','2','3','4','5','6','7','8','9','0','-','='};
                    if (ev.code >= 0x02 && ev.code <= 0x0D) {
                        c = map[ev.code];
                        if (c == '=') c = '+'; /* standard kbd */
                    }
                    if (ev.code == 0x1C) c = '\n'; /* Enter */
                    
                    /* Numpad support? scan codes 0x47.. */
                    
                    /* Map shifted + * ? Ignoring for now, just basics */
                    if (c != 0) handle_input(c);
                    
                    /* Support direct char if we had full map. */
                    /* Assume 'handle_input' takes char. */
                }
            }
        }
        
        /* Mouse */
        int mx, my, mbtn;
        wm_get_mouse_state(&mx, &my, &mbtn);
        if (mbtn && !last_btn) {
            /* Click! */
            int wx = g_calc->win->x;
            int wy = g_calc->win->y;
            if (mx > wx && mx < wx + g_calc->win->w && my > wy && my < wy + g_calc->win->h) {
                /* check buttons */
                int btn_w = (g_calc->win->w - 60) / 4;
                int btn_h = (g_calc->win->h - 100) / 4;
                
                for (int i = 0; i < 16; i++) {
                    int r = i / 4;
                    int c = i % 4;
                    int bx = wx + 20 + c * (btn_w + 5);
                    int by = wy + 80 + r * (btn_h + 5);
                    
                    if (mx >= bx && mx <= bx + btn_w && my >= by && my <= by + btn_h) {
                        const char *labels[] = {
                            "7", "8", "9", "/",
                            "4", "5", "6", "*",
                            "1", "2", "3", "-",
                            "C", "0", "=", "+"
                        };
                        handle_input(labels[i][0]);
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
    strcpy(st->display, "0");
    st->disp_len = 1;
    st->new_entry = 1;
    
    st->win = wm_create_window("Calculator", 200, 200, 300, 400, calc_draw);
    st->win->on_close = calc_on_close;
    
    g_calc = st;
    int calc_task_id = task_create(calc_task_entry, st, "calculator");
    
    /* CRITICAL: Reparent to init (1) so cascade kill from launcher doesn't kill us */
    task_set_parent(calc_task_id, 1);
}
