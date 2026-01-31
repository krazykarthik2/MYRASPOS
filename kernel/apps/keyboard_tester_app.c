#include "keyboard_tester_app.h"
#include "wm.h"
#include "kmalloc.h"
#include "lib.h"
#include "input.h"
#include "sched.h"
#include <string.h>

struct key_tester_state {
    char last_char;
    uint16_t last_code;
    char label[16];
    int has_key;
};

static void tester_render(struct window *win) {
    struct key_tester_state *st = (struct key_tester_state*)win->user_data;
    if (!st) return;
    
    /* Background and border are now partially handled by wm_draw_rect clipping,
       but we still draw our own inner styling relative to (0,0) of the content area. */
    wm_draw_rect(win, 5, 5, win->w - 14, win->h - 34, 0x1A1A1A);
    
    if (st->has_key) {
        char buf[32];
        
        /* Display Key Label / Char */
        wm_draw_text(win, 20, 30, "Key Pressed:", 0xAAAAAA, 1);
        
        const char *display_str = st->label;
        if (display_str[0] == '\0' && st->last_char >= 32 && st->last_char <= 126) {
            static char s[2];
            s[0] = st->last_char;
            s[1] = '\0';
            display_str = s;
        } else if (display_str[0] == '\0') {
            display_str = "NON-PRINT";
        }
        
        wm_draw_text(win, 140, 25, display_str, 0xFFFFFF, 3);
        
        /* Display ASCII */
        wm_draw_text(win, 20, 80, "ASCII Value:", 0xAAAAAA, 1);
        
        int val = (unsigned char)st->last_char;
        int i = 0;
        if (val == 0) buf[i++] = '0';
        else {
            char t[10];
            int tp = 0;
            int v = val;
            while (v > 0) { t[tp++] = (v % 10) + '0'; v /= 10; }
            while (tp > 0) buf[i++] = t[--tp];
        }
        buf[i] = '\0';
        wm_draw_text(win, 140, 75, buf, 0x55FF55, 2);
        
        /* Display Scan Code */
        wm_draw_text(win, 20, 120, "Scan Code:", 0xAAAAAA, 1);
        
        val = st->last_code;
        i = 0;
        if (val == 0) buf[i++] = '0';
        else {
            char t[10];
            int tp = 0;
            int v = val;
            while (v > 0) { t[tp++] = (v % 10) + '0'; v /= 10; }
            while (tp > 0) buf[i++] = t[--tp];
        }
        buf[i] = '\0';
        wm_draw_text(win, 140, 115, buf, 0xFF9500, 2);

    } else {
        wm_draw_text(win, 20, 70, "Press any key...", 0x888888, 1);
    }
}

static void tester_on_close(struct window *win) {
    if (win->user_data) {
        kfree(win->user_data);
        win->user_data = NULL;
    }
}

static void tester_task(void *arg) {
    struct window *win = (struct window *)arg;
    struct key_tester_state *st = (struct key_tester_state *)win->user_data;
    int shift_state = 0;
    
    while (win->user_data == st) {
        if (wm_is_focused(win)) {
            struct wm_input_event ev;
            int updated = 0;
            while (wm_pop_key_event(win, &ev)) {
                if (ev.type == INPUT_TYPE_KEY) {
                    if (ev.code == 0x2A || ev.code == 0x36) {
                        shift_state = ev.value;
                        if (ev.value == 1) {
                           strcpy(st->label, ev.code == 0x2A ? "L-SHIFT" : "R-SHIFT");
                           st->last_char = 0;
                           st->last_code = ev.code;
                           st->has_key = 1;
                           updated = 1;
                        }
                        continue;
                    }
                    
                    if (ev.value == 1) { /* Key Down */
                        char c = 0;
                        strcpy(st->label, "");
                        
                        static uint8_t s2a[] = {
                            0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
                            '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
                            0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
                            'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
                        };
                        static uint8_t s2as[] = {
                            0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
                            '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
                            0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
                            'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
                        };
                        
                        if (ev.code < sizeof(s2a)) {
                            c = shift_state ? s2as[ev.code] : s2a[ev.code];
                            if (ev.code == 1) strcpy(st->label, "ESC");
                            else if (ev.code == 14) strcpy(st->label, "BACKSP");
                            else if (ev.code == 15) { strcpy(st->label, "TAB"); c = 9; }
                            else if (ev.code == 28) strcpy(st->label, "ENTER");
                            else if (ev.code == 29) strcpy(st->label, "L-CTRL");
                            else if (ev.code == 42) strcpy(st->label, "L-SHIFT");
                            else if (ev.code == 54) strcpy(st->label, "R-SHIFT");
                            else if (ev.code == 56) strcpy(st->label, "L-ALT");
                            else if (ev.code == 57) strcpy(st->label, "SPACE");
                            else if (ev.code == 58) strcpy(st->label, "CAPSLK");
                        }
                        
                        if (ev.code >= 59 && ev.code <= 68) {
                            int f = ev.code - 58;
                            st->label[0] = 'F';
                            if (f < 10) { st->label[1] = f + '0'; st->label[2] = '\0'; }
                            else { st->label[1] = '1'; st->label[2] = '0'; st->label[3] = '\0'; }
                        }
                        else if (ev.code == 87) strcpy(st->label, "F11");
                        else if (ev.code == 88) strcpy(st->label, "F12");
                        else if (ev.code == 71) { strcpy(st->label, "KP7"); c = '7'; }
                        else if (ev.code == 72) { strcpy(st->label, "KP8"); c = '8'; }
                        else if (ev.code == 73) { strcpy(st->label, "KP9"); c = '9'; }
                        else if (ev.code == 75) { strcpy(st->label, "KP4"); c = '4'; }
                        else if (ev.code == 76) { strcpy(st->label, "KP5"); c = '5'; }
                        else if (ev.code == 77) { strcpy(st->label, "KP6"); c = '6'; }
                        else if (ev.code == 79) { strcpy(st->label, "KP1"); c = '1'; }
                        else if (ev.code == 80) { strcpy(st->label, "KP2"); c = '2'; }
                        else if (ev.code == 81) { strcpy(st->label, "KP3"); c = '3'; }
                        else if (ev.code == 82) { strcpy(st->label, "KP0"); c = '0'; }
                        else if (ev.code == 83) { strcpy(st->label, "KP."); c = '.'; }
                        else if (ev.code == 74) { strcpy(st->label, "KP-"); c = '-'; }
                        else if (ev.code == 78) { strcpy(st->label, "KP+"); c = '+'; }
                        else if (ev.code == 55) { strcpy(st->label, "KP*"); c = '*'; }
                        else if (ev.code == 98) { strcpy(st->label, "KP/"); c = '/'; }
                        else if (ev.code == 96) strcpy(st->label, "KPENT");
                        else if (ev.code == 103) strcpy(st->label, "UP");
                        else if (ev.code == 108) strcpy(st->label, "DOWN");
                        else if (ev.code == 105) strcpy(st->label, "LEFT");
                        else if (ev.code == 106) strcpy(st->label, "RIGHT");
                        else if (ev.code == 111) { strcpy(st->label, "DEL"); c = 0; }
                        else if (ev.code == 125) { strcpy(st->label, "META"); c = 0; }
                        
                        st->last_char = c;
                        st->last_code = ev.code;
                        st->has_key = 1;
                        updated = 1;
                    }
                }
            }
            if (updated) wm_request_render(win);
        }
        yield();
    }
    
    task_set_fn_null(task_current_id());
}

void keyboard_tester_app_start(void) {
    struct key_tester_state *st = kmalloc(sizeof(struct key_tester_state));
    memset(st, 0, sizeof(*st));
    
    struct window *win = wm_create_window("Keyboard Tester", 350, 350, 450, 250, tester_render);
    win->user_data = st;
    win->on_close = tester_on_close;
    
    int tid = task_create(tester_task, win, "keytester");
    task_set_parent(tid, 1);
}
