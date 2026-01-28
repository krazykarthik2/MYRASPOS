#ifndef CURSOR_H
#define CURSOR_H

void cursor_init(void);
void draw_cursor_overlay(int x, int y);
void restore_bg(void);
void save_bg(int nx, int ny);

#endif