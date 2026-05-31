#ifndef SCREEN_H
#define SCREEN_H

#include "types.h"

#define COLOR_BLACK         0
#define COLOR_BLUE          1
#define COLOR_GREEN         2
#define COLOR_CYAN          3
#define COLOR_RED           4
#define COLOR_MAGENTA       5
#define COLOR_BROWN         6
#define COLOR_LIGHT_GREY    7
#define COLOR_DARK_GREY     8
#define COLOR_LIGHT_BLUE    9
#define COLOR_LIGHT_GREEN   10
#define COLOR_LIGHT_CYAN    11
#define COLOR_LIGHT_RED     12
#define COLOR_LIGHT_MAGENTA 13
#define COLOR_LIGHT_BROWN   14
#define COLOR_WHITE         15

void screen_set_color(uint8_t fg, uint8_t bg);
void screen_putchar(char c);
void screen_write(const char* str);
void screen_write_hex(uint64_t val);
void screen_write_dec(uint64_t val);
void screen_clear(void);
void screen_get_cursor(int* x, int* y);
void screen_set_cursor(int x, int y);
void screen_set_redirect(void (*callback)(char));
void screen_enable_hw_cursor(int enable);
void screen_set_viewport(int x, int y, int w, int h);
void screen_clear_viewport(void);
void printf(const char* fmt, ...);

#endif
