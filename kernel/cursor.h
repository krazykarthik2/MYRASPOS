#define CURSOR_W 12
#define CURSOR_H 19

void cursor_init(void);
void draw_cursor_overlay(int x, int y);
void restore_bg(void);
void save_bg(int nx, int ny);
