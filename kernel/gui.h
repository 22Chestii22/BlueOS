#ifndef GUI_H
#define GUI_H

#include "types.h"

#define GUI_MAX_WINDOWS 8
#define GUI_MAX_BUTTONS 8
#define GUI_EVENT_QUEUE_SIZE 16

#define GUI_TITLE_HEIGHT 24
#define GUI_TASK_HEIGHT 40
#define GUI_RESIZE_BORDER 4

#define GUI_DESKTOP_COL  COL_W7_DESKTOP

#define W7_TASKBAR_H     40
#define W7_ORB_SIZE      30
#define W7_TRAY_W        160
#define W7_SHOWDESKTOP_W 4

#define W7_TITLE_GLOW    1
#define W7_GLASS_BLUR_RADIUS 8
#define W7_GLASS_TINT_ALPHA  160

#define W7_SM_HEADER_H   50
#define W7_SM_LEFT_W     180
#define W7_SM_RIGHT_W    150
#define W7_SM_TOTAL_W    (W7_SM_LEFT_W + W7_SM_RIGHT_W)
#define W7_SM_SEARCH_H   28
#define W7_SM_BOTTOM_H   32
#define W7_SM_ITEM_H     26

#define W7_BTN_CLOSE  0
#define W7_BTN_MAX    1
#define W7_BTN_MIN    2

typedef struct {
    int type;
    int mx, my;
    int buttons;
} gui_event_t;

typedef struct {
    int x, y, w;
    char label[12];
    void (*on_click)(int win_id, int btn_id);
} gui_button_t;

typedef struct {
    char title[24];
    int x, y, w, h;
    int visible;
    char* content;
    int cw, ch;
    int cursor_x, cursor_y;
    gui_button_t buttons[GUI_MAX_BUTTONS];
    int num_buttons;
    int minimized;
    int maximized;
    int restore_x, restore_y, restore_w, restore_h;
    uint32_t* pixels;
    int pw, ph;
    int dragging;
    int drag_off_x, drag_off_y;
    int drag_outline_x, drag_outline_y;
    int resizing;
    int resize_edge;
    void (*on_content_click)(int win_id, int mx, int my);
    gui_event_t event_queue[GUI_EVENT_QUEUE_SIZE];
    int event_head;
    int event_tail;
    int is_terminal;
    int has_aero_chrome;
    int btn_close_hover;
    int btn_max_hover;
    int btn_min_hover;
    int dirty;
    int dirty_x, dirty_y, dirty_w, dirty_h;
    int pixels_page_allocated;
} gui_window_t;

void gui_init(void);
int gui_create(const char* title, int w, int h);
void gui_putchar(int win_id, char c);
void gui_puts(int win_id, const char* str);
void gui_clear(int win_id);
int gui_add_button(int win_id, const char* label, int x, int y, int w, void (*cb)(int, int));
void gui_render(void);
void gui_set_active(int win_id);
void gui_set_content_click_callback(int win_id, void (*cb)(int, int, int));
void gui_set_title(int win_id, const char* title);
void gui_get_window_rect(int win_id, int* x, int* y, int* w, int* h);
int gui_create_terminal(const char* title, int w, int h);
int gui_get_terminal(void);
void gui_clear_terminal(void);
int gui_get_event(int win_id, gui_event_t* ev);
void gui_draw_rect(int win_id, int x, int y, int w, int h, uint32_t color);
void gui_draw_text(int win_id, int x, int y, const char* str, uint32_t fg, uint32_t bg);

void gui_minimize_window(int win_id);
void gui_maximize_window(int win_id);
void gui_restore_window(int win_id);
void gui_close_window(int win_id);
int gui_is_window_visible(int win_id);
void gui_set_window_pos(int win_id, int x, int y);
void gui_set_window_size(int win_id, int w, int h);
void gui_set_window_minimized(int win_id, int minimized);

extern volatile int cmd_should_exit;

#endif