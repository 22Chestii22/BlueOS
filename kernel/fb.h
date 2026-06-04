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

/* Windows XP Luna (Blue) theme colors */
#define COL_XP_TITLE_TOP     FB_RGB(0x00,0x5A,0xE0)
#define COL_XP_TITLE_BOTTOM  FB_RGB(0x00,0x78,0xFF)
#define COL_XP_TITLE_BORDER  FB_RGB(0x00,0x33,0x99)
#define COL_XP_TITLE_TEXT    COL_WHITE
#define COL_XP_TITLE_INACT_TOP    FB_RGB(0xA0,0xA0,0xA0)
#define COL_XP_TITLE_INACT_BOTTOM FB_RGB(0xC8,0xC8,0xC8)
#define COL_XP_TITLE_INACT_TEXT   FB_RGB(0x6B,0x6B,0x6B)

#define COL_XP_TASKBAR       FB_RGB(0x24,0x5E,0xDC)
#define COL_XP_TASKBAR_BORDER COL_XP_TITLE_BORDER
#define COL_XP_START_GREEN   FB_RGB(0x3C,0x99,0x00)

#define COL_XP_HIGHLIGHT     FB_RGB(0x31,0x6A,0xC5)
#define COL_XP_HIGHLIGHT_TEXT COL_WHITE

#define COL_XP_BTN_BORDER    FB_RGB(0x7F,0x9D,0xB9)
#define COL_XP_BTN_FACE      FB_RGB(0xEC,0xE9,0xD8)
#define COL_XP_BTN_HOVER     FB_RGB(0xCE,0xE1,0xF5)
#define COL_XP_BTN_SHADOW    FB_RGB(0xA0,0xA0,0xA0)

#define COL_XP_WINDOW_BORDER_ACTIVE  COL_XP_TITLE_BORDER
#define COL_XP_WINDOW_BORDER_INACT   FB_RGB(0xA0,0xA0,0xA0)

#define COL_XP_MENU_HIGHLIGHT COL_XP_HIGHLIGHT
#define COL_XP_MENU_BG       COL_WHITE
#define COL_XP_MENU_BORDER   FB_RGB(0x7F,0x9D,0xB9)

#define COL_XP_DESKTOP       FB_RGB(0x3A,0x6E,0xA5)

/* XP Start Menu colors */
#define COL_XP_SM_HEADER     FB_RGB(0x00,0x5A,0xE0)
#define COL_XP_SM_HEADER2    FB_RGB(0x00,0x4A,0xC0)
#define COL_XP_SM_LEFT_BG    FB_RGB(0xFF,0xFF,0xFF)
#define COL_XP_SM_RIGHT_BG   FB_RGB(0xD6,0xE4,0xF0)
#define COL_XP_SM_SEPARATOR  FB_RGB(0xC0,0xC0,0xC0)
#define COL_XP_SM_BOTTOM_BG  FB_RGB(0xE2,0xE6,0xEB)
#define COL_XP_SM_USER_TEXT  COL_WHITE
#define COL_XP_SM_LOGOFF_BG  FB_RGB(0xE2,0xE6,0xEB)
#define COL_XP_SM_LOGOFF_FG  COL_BLACK
#define COL_XP_SM_SHUTDOWN   FB_RGB(0xCC,0x33,0x33)

#endif