#ifndef WINMAN_H
#define WINMAN_H

#include "types.h"

#define WINMAN_MAX_WINDOWS 4
#define WINMAN_MAX_BUTTONS 4

#define BOX_TL 0xDA
#define BOX_TR 0xBF
#define BOX_BL 0xC0
#define BOX_BR 0xD9
#define BOX_H  0xC4
#define BOX_V  0xB3
#define BOX_TL2 0xC9
#define BOX_TR2 0xBB
#define BOX_BL2 0xC8
#define BOX_BR2 0xBC
#define BOX_H2  0xCD
#define BOX_V2  0xBA
#define BLOCK   0xDB

typedef struct {
    int x, y, w;
    char label[12];
    uint8_t fg, bg;
    void (*on_click)(int win_id, int btn_id);
} win_button_t;

typedef struct {
    char title[24];
    int x, y, w, h;
    int visible;
    uint8_t fg, bg;
    uint8_t title_fg, title_bg;
    char* content;
    int cw, ch;
    int cursor_x, cursor_y;
    win_button_t buttons[WINMAN_MAX_BUTTONS];
    int num_buttons;
} window_t;

void winman_init(void);
int winman_create(const char* title, int x, int y, int w, int h);
void winman_putchar(int win_id, char c);
void winman_puts(int win_id, const char* str);
void winman_clear(int win_id);
int winman_add_button(int win_id, const char* label, int x, int y, int w, void (*cb)(int, int));
void winman_render(void);
void winman_set_active(int win_id);

#endif
