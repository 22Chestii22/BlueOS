#ifndef FB_H
#define FB_H

#include "types.h"

typedef struct {
    uint64_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
} fb_info_t;

extern fb_info_t fb_info;

void fb_init(uint64_t phys_addr, uint32_t width, uint32_t height, uint32_t pitch, uint8_t bpp);
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t fb_getpixel(uint32_t x, uint32_t y);
void fb_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_drawchar(int x, int y, char c, uint32_t fg, uint32_t bg);
void fb_drawstring(int x, int y, const char* str, uint32_t fg, uint32_t bg);
void fb_blit(void);
void fb_draw_vline(int x, int y0, int y1, uint32_t color);
void fb_draw_hline(int y, int x0, int x1, uint32_t color);
void fb_backbuffer_alloc(void);
void fb_clear(uint32_t color);
void fb_bsod_panic(uint64_t num, uint64_t error_code, uint64_t rip);

#define FB_RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define FB_GET_R(c) (((c) >> 16) & 0xFF)
#define FB_GET_G(c) (((c) >> 8) & 0xFF)
#define FB_GET_B(c) ((c) & 0xFF)

#define COL_BLACK       FB_RGB(0,0,0)
#define COL_WHITE       FB_RGB(255,255,255)
#define COL_LIGHT_GRAY  FB_RGB(192,192,192)
#define COL_DARK_GRAY   FB_RGB(128,128,128)
#define COL_BLUE        FB_RGB(0,0,128)
#define COL_BRIGHT_BLUE FB_RGB(0,0,255)
#define COL_TEAL        FB_RGB(0,128,128)
#define COL_RED         FB_RGB(255,0,0)
#define COL_GREEN       FB_RGB(0,255,0)
#define COL_YELLOW      FB_RGB(255,255,0)
#define COL_CYAN        FB_RGB(0,255,255)
#define COL_MAGENTA     FB_RGB(255,0,255)
#define COL_WIN_BLUE    FB_RGB(1,11,163)
#define COL_WIN_BLUE2   FB_RGB(0,0,170)

#endif