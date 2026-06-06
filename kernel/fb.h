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
uint32_t* fb_get_backbuffer(void);
void fb_apply_desktop_bg(void);

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


/* Windows XP Luna (Blue) theme colors */
#define COL_XP_TITLE_TOP     FB_RGB(0x00,0x58,0xEE)
#define COL_XP_TITLE_BOTTOM  FB_RGB(0x00,0x30,0x92)
#define COL_XP_TITLE_BORDER  FB_RGB(0x00,0x30,0x92)
#define COL_XP_TITLE_TEXT    COL_WHITE
#define COL_XP_TITLE_INACT_TOP    FB_RGB(0x76,0x97,0xE7)
#define COL_XP_TITLE_INACT_BOTTOM FB_RGB(0xAB,0xBA,0xE3)
#define COL_XP_TITLE_INACT_TEXT   FB_RGB(0x40,0x40,0x40)

#define COL_XP_TASKBAR_TOP     FB_RGB(0x31,0x65,0xC4)
#define COL_XP_TASKBAR_BOTTOM  FB_RGB(0x19,0x41,0xA5)
#define COL_XP_TASKBAR_HIGHLIGHT FB_RGB(0x3F,0x7E,0xE0)
#define COL_XP_TASKBAR_BORDER  FB_RGB(0x31,0x68,0xD5)
#define COL_XP_TRAY_TOP     FB_RGB(0x13,0x9E,0xE9)
#define COL_XP_TRAY_BOTTOM  FB_RGB(0x09,0x5B,0xC9)
#define COL_XP_TRAY_HIGHLIGHT FB_RGB(0x18,0xBB,0xFF)
#define COL_XP_START_GREEN   FB_RGB(0x3C,0x99,0x00)

#define COL_XP_HIGHLIGHT     FB_RGB(0x31,0x6A,0xC5)
#define COL_XP_HIGHLIGHT_TEXT COL_WHITE

#define COL_XP_BTN_BORDER    FB_RGB(0x7F,0x9D,0xB9)
#define COL_XP_BTN_FACE      FB_RGB(0xEC,0xE9,0xD8)
#define COL_XP_BTN_HOVER     FB_RGB(0xCE,0xE1,0xF5)
#define COL_XP_BTN_SHADOW    FB_RGB(0xAC,0xA8,0x99)

#define COL_XP_WINDOW_BORDER_ACTIVE  FB_RGB(0x08,0x31,0xD9)
#define COL_XP_WINDOW_BORDER_INACT   FB_RGB(0x65,0x82,0xF5)

#define COL_XP_MENU_HIGHLIGHT COL_XP_HIGHLIGHT
#define COL_XP_MENU_BG       COL_WHITE
#define COL_XP_MENU_BORDER   FB_RGB(0x7F,0x9D,0xB9)

#define COL_XP_DESKTOP       FB_RGB(0x3A,0x6E,0xA5)

/* XP Start Menu colors */
#define COL_XP_SM_LEFT_BG    FB_RGB(0xFF,0xFF,0xFF)
#define COL_XP_SM_RIGHT_BG   FB_RGB(0xD6,0xE4,0xF0)
#define COL_XP_SM_BOTTOM_BG  FB_RGB(0xE2,0xE6,0xEB)
#define COL_XP_SM_USER_TEXT  COL_WHITE

/* XP caption button colors */
#define COL_XP_BTN_MIN_FACE    FB_RGB(0x22,0x63,0xD5)
#define COL_XP_BTN_MAX_FACE    FB_RGB(0x22,0x63,0xD5)
#define COL_XP_BTN_CLOSE_FACE  FB_RGB(0xCC,0x46,0x00)
#define COL_XP_BTN_CLOSE_HOV   FB_RGB(0xFF,0x60,0x20)

/* XP theme selector */
#define XP_THEME_BLUE   0
#define XP_THEME_SILVER 1
#define XP_THEME_OLIVE  2
extern int xp_theme;

/* XP Silver theme colors */
#define COL_XP_SILVER_TITLE_TOP       FB_RGB(0x8B,0x8B,0x9B)
#define COL_XP_SILVER_TITLE_BOTTOM    FB_RGB(0x65,0x65,0x7B)
#define COL_XP_SILVER_TITLE_BORDER    FB_RGB(0x55,0x55,0x6B)
#define COL_XP_SILVER_TITLE_INACT_TOP    FB_RGB(0xB5,0xB5,0xC1)
#define COL_XP_SILVER_TITLE_INACT_BOTTOM FB_RGB(0xD0,0xD0,0xD8)
#define COL_XP_SILVER_TASKBAR_TOP     FB_RGB(0x6B,0x6B,0x7F)
#define COL_XP_SILVER_TASKBAR_BOTTOM  FB_RGB(0x53,0x53,0x65)
#define COL_XP_SILVER_TASKBAR_BORDER  FB_RGB(0x55,0x55,0x6B)
#define COL_XP_SILVER_TRAY_TOP        FB_RGB(0xB0,0xB0,0xBC)
#define COL_XP_SILVER_TRAY_BOTTOM     FB_RGB(0x91,0x91,0xA0)
#define COL_XP_SILVER_HIGHLIGHT       FB_RGB(0x6B,0x6B,0x7F)
#define COL_XP_SILVER_BTN_BORDER      FB_RGB(0x93,0x93,0xA1)
#define COL_XP_SILVER_BTN_FACE        FB_RGB(0xE8,0xE8,0xE0)
#define COL_XP_SILVER_BTN_SHADOW      FB_RGB(0xA8,0xA8,0xB0)
#define COL_XP_SILVER_WINDOW_BORDER_ACTIVE  FB_RGB(0x4B,0x4B,0x5F)
#define COL_XP_SILVER_WINDOW_BORDER_INACT   FB_RGB(0x8B,0x8B,0x99)
#define COL_XP_SILVER_DESKTOP         FB_RGB(0x3A,0x6E,0xA5)
#define COL_XP_SILVER_MENU_BORDER     FB_RGB(0x93,0x93,0xA1)
#define COL_XP_SILVER_SM_LEFT_BG      FB_RGB(0xFF,0xFF,0xFF)
#define COL_XP_SILVER_SM_RIGHT_BG     FB_RGB(0xE8,0xE8,0xE5)
#define COL_XP_SILVER_SM_BOTTOM_BG    FB_RGB(0xE8,0xE8,0xE5)

/* XP Olive theme colors */
#define COL_XP_OLIVE_TITLE_TOP        FB_RGB(0x7B,0x8C,0x3E)
#define COL_XP_OLIVE_TITLE_BOTTOM     FB_RGB(0x52,0x61,0x29)
#define COL_XP_OLIVE_TITLE_BORDER     FB_RGB(0x42,0x51,0x19)
#define COL_XP_OLIVE_TITLE_INACT_TOP     FB_RGB(0xAD,0xB5,0x84)
#define COL_XP_OLIVE_TITLE_INACT_BOTTOM  FB_RGB(0xC7,0xCC,0xA5)
#define COL_XP_OLIVE_TASKBAR_TOP      FB_RGB(0x63,0x72,0x31)
#define COL_XP_OLIVE_TASKBAR_BOTTOM   FB_RGB(0x4A,0x57,0x22)
#define COL_XP_OLIVE_TASKBAR_BORDER   FB_RGB(0x4A,0x57,0x22)
#define COL_XP_OLIVE_TRAY_TOP         FB_RGB(0x93,0xA0,0x60)
#define COL_XP_OLIVE_TRAY_BOTTOM      FB_RGB(0x7B,0x89,0x4B)
#define COL_XP_OLIVE_HIGHLIGHT        FB_RGB(0x63,0x72,0x31)
#define COL_XP_OLIVE_BTN_BORDER       FB_RGB(0x8C,0x97,0x62)
#define COL_XP_OLIVE_BTN_FACE         FB_RGB(0xE8,0xE8,0xD0)
#define COL_XP_OLIVE_BTN_SHADOW       FB_RGB(0xA0,0xA8,0x80)
#define COL_XP_OLIVE_WINDOW_BORDER_ACTIVE  FB_RGB(0x42,0x51,0x19)
#define COL_XP_OLIVE_WINDOW_BORDER_INACT   FB_RGB(0x8C,0x97,0x62)
#define COL_XP_OLIVE_DESKTOP          FB_RGB(0x3A,0x6E,0xA5)
#define COL_XP_OLIVE_MENU_BORDER      FB_RGB(0x8C,0x97,0x62)
#define COL_XP_OLIVE_SM_LEFT_BG       FB_RGB(0xFF,0xFF,0xFF)
#define COL_XP_OLIVE_SM_RIGHT_BG      FB_RGB(0xE8,0xE8,0xD8)
#define COL_XP_OLIVE_SM_BOTTOM_BG     FB_RGB(0xE8,0xE8,0xD8)

/* Alpha blending functions */
uint32_t fb_blend(uint32_t fg, uint32_t bg, uint8_t alpha);
void fb_fillrect_alpha(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint8_t alpha);


/* Region save/restore */
void fb_save_region(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t* buf);
void fb_restore_region(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t* buf);

#endif
