#ifndef GUI_H
#define GUI_H

#include "types.h"

#define GUI_MAX_WINDOWS 4
#define GUI_MAX_BUTTONS 4
#define GUI_EVENT_QUEUE_SIZE 16

#define GUI_MENU_HEIGHT 18
#define GUI_STATUS_HEIGHT 18
#define GUI_TITLE_HEIGHT 18
#define GUI_BORDER_WIDTH 2
#define GUI_TILE_MASTER_RATIO 60

#define GUI_DESKTOP_COL  0x00002060

#define GUI_MAX_MENUS 4
#define GUI_MAX_MENU_ITEMS 8
#define GUI_MENU_DROPDOWN_W 120

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
    char label[16];
    int enabled;
} gui_menu_item_t;

typedef struct {
    char label[16];
    int x;
    int is_open;
    int hovered;
    gui_menu_item_t items[GUI_MAX_MENU_ITEMS];
    int num_items;
} gui_menu_t;

typedef struct {
    char title[24];
    int x, y, w, h;
    int visible;
    char* content;
    int cw, ch;
    int cursor_x, cursor_y;
    gui_button_t buttons[GUI_MAX_BUTTONS];
    int num_buttons;
    void (*on_content_click)(int win_id, int mx, int my);
    gui_event_t event_queue[GUI_EVENT_QUEUE_SIZE];
    int event_head;
    int event_tail;
} gui_window_t;

void gui_init(void);
int gui_create(const char* title, int w, int h);
void gui_putchar(int win_id, char c);
void gui_puts(int win_id, const char* str);
void gui_clear(int win_id);
int gui_add_button(int win_id, const char* label, int x, int y, int w, void (*cb)(int, int));
void gui_render(void);
void gui_set_active(int win_id);
void gui_menu_init(void);
void gui_set_content_click_callback(int win_id, void (*cb)(int, int, int));
void gui_set_title(int win_id, const char* title);
void gui_get_window_rect(int win_id, int* x, int* y, int* w, int* h);
int gui_create_terminal(const char* title, int w, int h);
int gui_get_terminal(void);
void gui_clear_terminal(void);
int gui_get_event(int win_id, gui_event_t* ev);

extern volatile int cmd_should_exit;

#endif
