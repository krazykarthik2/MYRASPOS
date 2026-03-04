#include "wm.h"
#include "framebuffer.h"
#include "sched.h"
#include "kmalloc.h"
#include "input.h"
#include "virtio.h"
#include "uart.h"
#include "timer.h"
#include "cursor.h"

static uint32_t bg_buffer[CURSOR_W * CURSOR_H];
static int last_x = -1, last_y = -1;

void restore_bg(void) {
    if (last_x == -1) return;
    for (int y = 0; y < CURSOR_H; y++) {
        for (int x = 0; x < CURSOR_W; x++) {
            fb_set_pixel(last_x + x, last_y + y, bg_buffer[y * CURSOR_W + x]);
        }
    }
}

void save_bg(int nx, int ny) {
    for (int y = 0; y < CURSOR_H; y++) {
        for (int x = 0; x < CURSOR_W; x++) {
            bg_buffer[y * CURSOR_W + x] = fb_get_pixel(nx + x, ny + y);
        }
    }
    last_x = nx;
    last_y = ny;
}

void draw_cursor_overlay(int x, int y) {
    /* Better arrow cursor (matching wm.c but as overlay) */
    uint32_t c = 0xFFFFFFFF;     /* white (with alpha) */
    uint32_t o = 0xFF000000;     /* black outline (with alpha) */

    /* Outline */
    for(int i=0; i<12; i++) fb_set_pixel(x+0, y+i, o);
    for(int i=0; i<8; i++)  fb_set_pixel(x+i, y+i, o);
    for(int i=0; i<5; i++)  fb_set_pixel(x+i, y+8, o);
    fb_set_pixel(x+5, y+9, o);
    fb_set_pixel(x+6, y+10, o);
    fb_set_pixel(x+7, y+11, o);
    fb_set_pixel(x+1, y+12, o);

    /* Fill */
    for(int i=1; i<11; i++) fb_set_pixel(x+1, y+i, c);
    for(int i=2; i<7; i++)  fb_set_pixel(x+2, y+i, c);
    for(int i=3; i<6; i++)  fb_set_pixel(x+3, y+i, c);
    fb_set_pixel(x+4, y+4, c);
}

void cursor_task(void *arg) {
    (void)arg;
    int screen_w, screen_h;
    fb_get_res(&screen_w, &screen_h);
    
    int mx = screen_w / 2;
    int my = screen_h / 2;
    
    while (1) {
        /* Wait for mouse event from IRQ or WM */
        task_wait_event(MOUSE_EVENT_ID);
        // uart_puts("Mouse event received\n");
        int nx, ny, btn;
        input_get_mouse_state(&nx, &ny, &btn);
        
        if (nx != last_x || ny != last_y) {
            /* Now handled by WM to ensure top-most layering */
            task_wake_event(WM_EVENT_ID);
        }
    }
}

void mouse_sim_task(void *arg) {
    (void)arg;
    int step = 0;
    int dir = 0; // 0: Right, 1: Down, 2: Left, 3: Up
    
    while (1) {
        timer_sleep_ms(50);
        
        int dx = 0, dy = 0;
        if (dir == 0) dx = 5;
        else if (dir == 1) dy = 5;
        else if (dir == 2) dx = -5;
        else if (dir == 3) dy = -5;
        
        input_push_event(INPUT_TYPE_REL, 0, dx);
        input_push_event(INPUT_TYPE_REL, 1, dy);
        
        step++;
        if (step >= 40) {
            step = 0;
            dir = (dir + 1) % 4;
        }
    }
}

void cursor_init(void) {
    task_create_with_stack(cursor_task, NULL, "cursor_overlay", 16);
#ifdef REAL
    task_create(mouse_sim_task, NULL, "mouse_sim");
#endif
}
