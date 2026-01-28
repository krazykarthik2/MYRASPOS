#include "myra_app.h"
#include "wm.h"
#include "framebuffer.h"
#include "kmalloc.h"
#include "lib.h"
#include "input.h"
#include "terminal_app.h"
#include "calculator_app.h"
#include "sched.h"
#include "timer.h"
#include <string.h>
#include "uart.h"
#include "files_app.h"
#include "keyboard_tester_app.h"
#include "editor_app.h"

struct app_info {
    const char *name;
    void (*launch)(void);
    const char *icon_path;
    int dist; /* for search results */
};

/* Mock applications */
static void launch_terminal(void) { terminal_app_start(); }

static void launch_files(void) { files_app_start(); }

static void settings_draw(struct window *win) {
    fb_draw_text(win->x + 20, win->y + 40, "System Settings", 0xAAAAAA, 2);
    fb_draw_hline(win->x + 20, win->x + win->w - 20, win->y + 65, 0x444444);
    fb_draw_text(win->x + 30, win->y + 90, "Display: 1280x800", 0xFFFFFF, 1);
    fb_draw_text(win->x + 30, win->y + 120, "Theme: Steel Blue", 0xFFFFFF, 1);
    fb_draw_text(win->x + 30, win->y + 150, "Kernel: MYRAS 0.1", 0xFFFFFF, 1);
}
static void launch_settings(void) { wm_create_window("Settings", 150, 150, 350, 250, settings_draw); }

static void help_draw(struct window *win) {
    fb_draw_text(win->x + 20, win->y + 40, "Help & Documentation", 0xAAAAAA, 2);
    fb_draw_hline(win->x + 20, win->x + win->w - 20, win->y + 65, 0x444444);
    fb_draw_text(win->x + 30, win->y + 80, "Welcome to Valli OS!", 0x00FF00, 1);
    fb_draw_text(win->x + 30, win->y + 110, "- Use Arrows to move cursor", 0xFFFFFF, 1);
    fb_draw_text(win->x + 30, win->y + 130, "- Enter/Space to click", 0xFFFFFF, 1);
    fb_draw_text(win->x + 30, win->y + 150, "- Shift for speed", 0xFFFFFF, 1);
}
static void launch_help(void) { wm_create_window("Help", 180, 180, 400, 300, help_draw); }

static void launch_calculator(void) { calculator_app_start(); }
static void launch_keytester(void) { keyboard_tester_app_start(); }
static void launch_editor(void) { editor_app_start(NULL); }

static struct app_info apps[] = {
    {"Terminal", launch_terminal, "/icons/terminal.bin", 0},
    {"Calculator", launch_calculator, "/icons/calc.bin", 0},
    {"Keyboard Tester", launch_keytester, "/icons/keys.bin", 0},
    {"File Explorer", launch_files, "/icons/files.bin", 0},
    {"Valli Editor", launch_editor, "/icons/editor.bin", 0},
    {"Settings", launch_settings, "/icons/settings.bin", 0},
    {"Help", launch_help, "/icons/help.bin", 0}
};
#define NUM_APPS (sizeof(apps)/sizeof(apps[0]))

struct myra_app_state {
    struct window *win;
    char search_query[64];
    int query_len;
    struct app_info *filtered_apps[NUM_APPS];
    int num_filtered;
    int cursor_visible;
    uint32_t last_blink;
};

static struct myra_app_state *g_myra = NULL;

static int get_search_score(char *input, const char *candidate) {
    /* 1. Exact Match (Best) - Case Insensitive */
    if (levenshtein_distance_ci(input, candidate) == 0) return 0;

    /* 2. Prefix Match - Case Insensitive */
    char *p = strcasestr(candidate, input);
    if (p == candidate) {
        /* Smaller penalty for prefix match, weight by length difference */
        return 1 + (int)(strlen(candidate) - strlen(input));
    }

    /* 3. Contains Match - Case Insensitive */
    if (p) {
        /* Larger penalty for mid-string match */
        return 5 + (int)(strlen(candidate) - strlen(input));
    }

    /* 4. Fuzzy Match - Case Insensitive */
    int dist = levenshtein_distance_ci(input, candidate);
    if (dist < 3) return 10 + dist; /* Only allow very close fuzzy matches */

    return 100; /* No match */
}

static void myra_on_close(struct window *win) {
    (void)win;
    if (g_myra) g_myra = NULL;
}

static void update_search(void) {
    if (g_myra->query_len == 0) {
        g_myra->num_filtered = NUM_APPS;
        for (size_t i = 0; i < NUM_APPS; i++) g_myra->filtered_apps[i] = &apps[i];
    } else {
        int count = 0;
        for (size_t i = 0; i < NUM_APPS; i++) {
            int score = get_search_score(g_myra->search_query, apps[i].name);
            if (score <= 15) {
                apps[i].dist = score;
                g_myra->filtered_apps[count++] = &apps[i];
            }
        }
        /* Sort by score (Stable Bubble Sort) */
        for (int i = 0; i < count - 1; i++) {
            for (int j = 0; j < count - i - 1; j++) {
                if (g_myra->filtered_apps[j]->dist > g_myra->filtered_apps[j+1]->dist) {
                    struct app_info *temp = g_myra->filtered_apps[j];
                    g_myra->filtered_apps[j] = g_myra->filtered_apps[j+1];
                    g_myra->filtered_apps[j+1] = temp;
                }
            }
        }
        g_myra->num_filtered = count;
    }
}

static void myra_draw(struct window *win) {
    if (!g_myra) return;
    // uart_puts("[myra] drawing\n");

    /* Blink logic (simulated here for simplicity, or done in task and state updated) */
    /* Actually task updates visible state, we just draw */
    
    /* Search Bar Background */
    int sb_x = win->w/2 - 100;
    wm_draw_rect(win, sb_x, 8, 200, 25, 0x333333);
    
    /* Search Text */
    int text_x = sb_x + 5;
    int text_y = 13;
    if (g_myra->query_len > 0) {
        wm_draw_text(win, text_x, text_y, g_myra->search_query, 0xFFFFFF, 1);
    } else {
        wm_draw_text(win, text_x, text_y, "Search...", 0x888888, 1);
    }

    /* Grid layout 6x6 */
    int cell_w = win->w / 6;
    int cell_h = (win->h - 60) / 6;

    // uart_puts("[myra] drawing grid\n");
    for (int i = 0; i < g_myra->num_filtered; i++) {
        int r = i / 6;
        int c = i % 6;
        int x = c * cell_w + 10;
        int y = 43 + r * cell_h + 10;

        /* App Icon placeholder */
        wm_draw_rect(win, x, y, 40, 40, 0x555555);
        
        /* App Name */
        if (g_myra->filtered_apps[i] && g_myra->filtered_apps[i]->name) {
             wm_draw_text(win, x, y + 45, g_myra->filtered_apps[i]->name, 0xFFFFFF, 1);
        }
    }
    // uart_puts("[myra] done drawing\n");
}

static void myra_task(void *arg) {
    // uart_puts("[myra] task started\n");
    (void)arg;
    int shift_state = 0;
    /* Initialize Blink */
    if (g_myra) {
        g_myra->cursor_visible = 1;
        g_myra->last_blink = timer_get_ms(); // Ensure timer.h is included
    }

    while (g_myra) {
        /* Blink update */
        uint32_t now = timer_get_ms();
        if (now - g_myra->last_blink > 500) {
            g_myra->cursor_visible = !g_myra->cursor_visible;
            g_myra->last_blink = now;
            wm_request_render(g_myra->win);
        }

        /* Poll keyboard for search bar if focused */
        if (wm_is_focused(g_myra->win)) {
            struct wm_input_event ev;
            while (wm_pop_key_event(g_myra->win, &ev)) {
                if (ev.type == INPUT_TYPE_KEY) {
                    /* Shift keys: 0x2A (LShift), 0x36 (RShift) */
                    if (ev.code == 0x2A || ev.code == 0x36) {
                        shift_state = ev.value;
                        continue;
                    }
                    if (ev.value == 1) { /* key press only */
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

                        if (ev.code == 0x0E) { /* Backspace */
                            if (g_myra->query_len > 0) g_myra->search_query[--g_myra->query_len] = '\0';
                        } else if (ev.code == 0x1C || ev.code == 0x9C) { /* Enter (Standard or KP) */
                            if (g_myra->num_filtered > 0 && g_myra->filtered_apps[0]) {
                                g_myra->filtered_apps[0]->launch();
                                wm_close_window(g_myra->win);
                                /* update_search(); REMOVED: causes panic because g_myra is NULL after close */
                                break; 
                            }
                        } else if (ev.code < sizeof(s2a)) {
                            char c = shift_state ? s2as[ev.code] : s2a[ev.code];
                            if (c >= 32 && c <= 126 && g_myra->query_len < 63) {
                                g_myra->search_query[g_myra->query_len++] = c;
                                g_myra->search_query[g_myra->query_len] = '\0';
                            }
                        }
                        update_search();
                        wm_request_render(g_myra->win);
                    }
                }
            }
        }

        /* Check for mouse clicks on apps */
        int mx, my, mbtn;
        wm_get_mouse_state(&mx, &my, &mbtn);
        static int last_btn = 0;
        if (mbtn && !last_btn) {
            int win_x = g_myra->win->x;
            int win_y = g_myra->win->y;
            int win_w = g_myra->win->w;
            int win_h = g_myra->win->h;
            
            if (mx >= win_x && mx <= win_x + win_w && my >= win_y + 65 && my <= win_y + win_h) {
                int cell_w = win_w / 6;
                int cell_h = (win_h - 60) / 6;
                int c = (mx - win_x) / cell_w;
                int r = (my - (win_y + 65)) / cell_h;
                int idx = r * 6 + c;
                if (idx >= 0 && idx < g_myra->num_filtered) {
                    /* Launch the app (it will reparent itself to init) */
                    g_myra->filtered_apps[idx]->launch();
                    
                    /* Close Myra */
                    wm_close_window(g_myra->win);
                    break;
                }
            }
        }
        last_btn = mbtn;

        yield();
    }
    /* Cleanup state */
    /* We stored 'arg' which IS g_myra (or was). */
    struct myra_app_state *st = (struct myra_app_state *)arg;
    g_myra = NULL; /* Just in case */
    kfree(st);
    task_set_fn_null(task_current_id());
}

void myra_app_open(void) {
    if (g_myra) return; /* Already open */

    g_myra = kmalloc(sizeof(struct myra_app_state));
    if (!g_myra) return;
    memset(g_myra, 0, sizeof(*g_myra));
    
    int w = 500, h = 400;
    int screen_w, screen_h;
    fb_get_res(&screen_w, &screen_h);
    
    g_myra->win = wm_create_window("Valli Launcher", (screen_w - w)/2, (screen_h - h)/2, w, h, myra_draw);
    g_myra->win->on_close = myra_on_close;
    g_myra->num_filtered = NUM_APPS;
    for (size_t i = 0; i < NUM_APPS; i++) g_myra->filtered_apps[i] = &apps[i];

    /* Pass g_myra as arg so task can free it */
    task_create(myra_task, g_myra, "myra_app");
}

static void editor_app_start_null(void) {
    editor_app_start(NULL);
}

void myra_app_toggle(void) {
    if (g_myra) {
        wm_close_window(g_myra->win);
    } else {
        myra_app_open();
    }
}
