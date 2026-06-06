#include "types.h"
#include "string.h"
#include "mem.h"
#include "screen.h"
#include "fb.h"
#include "gui.h"
#include "font.h"
#include "blu.h"
#include "module.h"
#include "timer.h"
#include "paging.h"
#include "process.h"

static gui_window_t windows[GUI_MAX_WINDOWS];
static int num_windows = 0;
static int active_window = -1;
static uint8_t prev_buttons = 0;
static int initialized = 0;

volatile int cmd_should_exit = 0;

static int gui_terminal_win = -1;
static int cascade_x = 20;
static int cascade_y = 40;

static void draw_xp_scrollbar(int x, int y, int w, int h, int scroll_off, int scroll_max, int visible_rows);

static int start_menu_open = 0;
static int start_menu_hovered = -1;

static int screen_dirty = 1;
static int screen_dirty_x = 0, screen_dirty_y = 0;
static int screen_dirty_w = 0, screen_dirty_h = 0;

#define GUI_PIXEL_VADDR_BASE 0x3000000

static void mark_screen_dirty(int x, int y, int w, int h)
{
    if (!screen_dirty)
    {
        screen_dirty = 1;
        screen_dirty_x = x;
        screen_dirty_y = y;
        screen_dirty_w = w;
        screen_dirty_h = h;
        return;
    }
    int x1 = screen_dirty_x < x ? screen_dirty_x : x;
    int y1 = screen_dirty_y < y ? screen_dirty_y : y;
    int x2 = (screen_dirty_x + screen_dirty_w) > (x + w) ? (screen_dirty_x + screen_dirty_w) : (x + w);
    int y2 = (screen_dirty_y + screen_dirty_h) > (y + h) ? (screen_dirty_y + screen_dirty_h) : (y + h);
    screen_dirty_x = x1;
    screen_dirty_y = y1;
    screen_dirty_w = x2 - x1;
    screen_dirty_h = y2 - y1;
}

static void mark_window_dirty(int win_id, int x, int y, int w, int h)
{
    if (win_id < 0 || win_id >= GUI_MAX_WINDOWS) return;
    gui_window_t* win = &windows[win_id];
    int ax = win->x + x;
    int ay = win->y + GUI_TITLE_HEIGHT + y;
    if (!win->dirty)
    {
        win->dirty = 1;
        win->dirty_x = ax;
        win->dirty_y = ay;
        win->dirty_w = w;
        win->dirty_h = h;
    }
    else
    {
        int x1 = win->dirty_x < ax ? win->dirty_x : ax;
        int y1 = win->dirty_y < ay ? win->dirty_y : ay;
        int x2 = (win->dirty_x + win->dirty_w) > (ax + w) ? (win->dirty_x + win->dirty_w) : (ax + w);
        int y2 = (win->dirty_y + win->dirty_h) > (ay + h) ? (win->dirty_y + win->dirty_h) : (ay + h);
        win->dirty_x = x1;
        win->dirty_y = y1;
        win->dirty_w = x2 - x1;
        win->dirty_h = y2 - y1;
    }
    mark_screen_dirty(ax, ay, w, h);
}

static uint32_t* gui_alloc_pages(uint32_t size)
{
    uint32_t pages = (size + 0xFFF) / 0x1000;
    static uint64_t next_vaddr = GUI_PIXEL_VADDR_BASE;
    uint64_t vaddr = next_vaddr;

    for (uint32_t i = 0; i < pages; i++)
    {
        uint64_t paddr = paging_alloc_frame();
        if (paddr == 0xFFFFFFFF)
        {
            for (uint32_t j = 0; j < i; j++)
                unmap_page(vaddr + j * 0x1000);
            return NULL;
        }
        map_page_cr3(kernel_cr3, vaddr + i * 0x1000, paddr, 0x03);
        paging_map_all_processes(vaddr + i * 0x1000, paddr, 0x07);
    }

    next_vaddr = vaddr + pages * 0x1000;
    return (uint32_t*)vaddr;
}

static int gui_pixel_alloc(gui_window_t* w)
{
    uint64_t alloc_size64 = (uint64_t)w->pw * (uint64_t)w->ph * 4;
    if (alloc_size64 > 0xFFFFFFFF)
    {
        printf("[GUI] Pixel buffer too large for '%s' (%llu bytes)\n", w->title, alloc_size64);
        return 0;
    }
    uint32_t alloc_size = (uint32_t)alloc_size64;
    w->pixels = malloc(alloc_size);
    w->pixels_page_allocated = 0;
    if (w->pixels)
    {
        memset(w->pixels, 0xFF, alloc_size);
        return 1;
    }
    w->pixels = gui_alloc_pages(alloc_size);
    if (w->pixels)
    {
        memset(w->pixels, 0xFF, alloc_size);
        w->pixels_page_allocated = 1;
        printf("[GUI] Window '%s' pixel buffer page-allocated (%d KB)\n", w->title, alloc_size / 1024);
        return 1;
    }
    printf("[GUI] FAILED to allocate pixel buffer for '%s' (%d KB)\n", w->title, alloc_size / 1024);
    return 0;
}

static const char* start_left_items[] = {
    "CMD", NULL
};
static const char* start_left_paths[] = {
    "\\SYSTEM\\PROGRAMS\\CMD.BLU",
};
static int start_left_count = 1;

static const char* start_right_items[] = {
    "Computer", "Help & Support", NULL
};
static int start_right_count = 2;

typedef struct {
    const char* label;
    const char* path;
    int x, y, w, h;
} desktop_icon_t;

static desktop_icon_t desktop_icons[] = {};
static int num_desktop_icons = 0;

int gui_create_terminal(const char* title, int w, int h)
{
    gui_terminal_win = gui_create(title, w, h);
    if (gui_terminal_win >= 0)
        windows[gui_terminal_win].is_terminal = 1;
    return gui_terminal_win;
}

int gui_get_terminal(void) { return gui_terminal_win; }

void gui_clear_terminal(void)
{
    if (gui_terminal_win >= 0)
        gui_clear(gui_terminal_win);
}

static void bring_to_front(int idx)
{
    if (idx < 0 || idx >= num_windows) return;
    if (idx == num_windows - 1) return;
    gui_window_t tmp = windows[idx];
    for (int i = idx; i < num_windows - 1; i++)
        windows[i] = windows[i + 1];
    windows[num_windows - 1] = tmp;
    active_window = num_windows - 1;
}

static void draw_desktop_icons(void)
{
    for (int i = 0; i < num_desktop_icons; i++)
    {
        desktop_icon_t* di = &desktop_icons[i];
        fb_fillrect_alpha(di->x, di->y, di->w, di->h, FB_RGB(0x40, 0x80, 0xC0), 50);
        fb_fillrect(di->x + 8, di->y + 4, 48, 40, FB_RGB(0x20, 0x60, 0xA0));
        fb_drawstring(di->x + 4, di->y + 48, di->label, COL_BLACK, 0);
        fb_drawstring(di->x + 3, di->y + 47, di->label, COL_WHITE, 0);
    }
}

static void draw_desktop(void)
{
    fb_apply_desktop_bg();
}

static void draw_window_shadow(gui_window_t* w)
{
    int s = 5;
    int x = w->x, y = w->y, ww = w->w, wh = w->h;
    for (int i = 1; i <= s; i++)
    {
        uint8_t a = 55 - (uint32_t)(i * 45) / s;
        fb_fillrect_alpha(x + ww, y + i, i, wh + 2 - i, COL_BLACK, a);
        fb_fillrect_alpha(x + i, y + wh, ww - i + 1, i, COL_BLACK, a);
        fb_fillrect_alpha(x + ww, y + wh, i, i, COL_BLACK, a);
    }
}

static void fb_draw_rect_outline(int x, int y, int w, int h, uint32_t color)
{
    fb_draw_hline(y, x, x + w - 1, color);
    fb_draw_hline(y + h - 1, x, x + w - 1, color);
    fb_draw_vline(x, y, y + h - 1, color);
    fb_draw_vline(x + w - 1, y, y + h - 1, color);
}

static void draw_xp_button_3d(int x, int y, int w, int h, const char* text, uint32_t face, int pressed)
{
    if (!pressed)
    {
        fb_draw_rect_outline(x, y, w, h, COL_XP_BTN_BORDER);
        fb_fillrect(x + 1, y + 1, w - 2, h - 2, face);
        fb_draw_hline(y + 1, x + 1, x + w - 2, COL_WHITE);
        fb_draw_vline(x + 1, y + 1, y + h - 2, COL_WHITE);
        fb_draw_hline(y + h - 2, x + 2, x + w - 3, FB_RGB(0x80, 0x80, 0x80));
        fb_draw_vline(x + w - 2, y + 2, y + h - 3, FB_RGB(0x80, 0x80, 0x80));
    }
    else
    {
        fb_draw_rect_outline(x, y, w, h, COL_XP_BTN_SHADOW);
        fb_fillrect(x + 1, y + 1, w - 2, h - 2, face);
        fb_draw_hline(y + 1, x + 1, x + w - 2, COL_XP_BTN_SHADOW);
        fb_draw_vline(x + 1, y + 1, y + h - 2, COL_XP_BTN_SHADOW);
    }

    if (text)
    {
        int tx = x + (w - (int)strlen(text) * FONT_WIDTH) / 2;
        int ty = y + (h - FONT_HEIGHT) / 2;
        fb_drawstring(tx, ty, text, COL_BLACK, face);
    }
}

static void draw_xp_title_text(int x, int y, const char* str, uint32_t fg, uint32_t shadow)
{
    int cx;
    if (shadow)
    {
        cx = x + 1;
        for (int i = 0; str[i]; i++)
        {
            int ci = (unsigned char)str[i] - FONT_FIRST_CHAR;
            if (ci < 0 || ci >= FONT_NUM_CHARS) { cx += FONT_WIDTH; continue; }
            for (int row = 0; row < FONT_HEIGHT; row++)
            {
                unsigned char bits = font_data[ci][row];
                for (int col = 0; col < FONT_WIDTH; col++)
                {
                    int px = cx + col, py = y + 1 + row;
                    if (px >= 0 && (uint32_t)px < fb_info.width && py >= 0 && (uint32_t)py < fb_info.height)
                        if (bits & (1 << (7 - col)))
                            fb_putpixel(px, py, shadow);
                }
            }
            cx += FONT_WIDTH;
        }
    }
    cx = x;
    for (int i = 0; str[i]; i++)
    {
        int ci = (unsigned char)str[i] - FONT_FIRST_CHAR;
        if (ci < 0 || ci >= FONT_NUM_CHARS) { cx += FONT_WIDTH; continue; }
        for (int row = 0; row < FONT_HEIGHT; row++)
        {
            unsigned char bits = font_data[ci][row];
            for (int col = 0; col < FONT_WIDTH; col++)
            {
                int px = cx + col, py = y + row;
                if (px >= 0 && (uint32_t)px < fb_info.width && py >= 0 && (uint32_t)py < fb_info.height)
                    if (bits & (1 << (7 - col)))
                        fb_putpixel(px, py, fg);
            }
        }
        cx += FONT_WIDTH;
    }
}

static void draw_xp_title_bar(gui_window_t* w, int active)
{
    int x = w->x, y = w->y, tw = w->w;
    int title_h = GUI_TITLE_HEIGHT;

    static const uint8_t cr_mask[7] = {6, 6, 5, 4, 3, 1, 0};

    for (int row = 0; row < title_h - 1; row++)
    {
        uint32_t color;
        if (active)
        {
            uint8_t r = ((COL_XP_TITLE_TOP >> 16) & 0xFF) +
                (((uint32_t)(((COL_XP_TITLE_BOTTOM >> 16) & 0xFF) - ((COL_XP_TITLE_TOP >> 16) & 0xFF))) * row / (title_h - 2));
            uint8_t g = ((COL_XP_TITLE_TOP >> 8) & 0xFF) +
                (((uint32_t)(((COL_XP_TITLE_BOTTOM >> 8) & 0xFF) - ((COL_XP_TITLE_TOP >> 8) & 0xFF))) * row / (title_h - 2));
            uint8_t b = (COL_XP_TITLE_TOP & 0xFF) +
                (((uint32_t)((COL_XP_TITLE_BOTTOM & 0xFF) - (COL_XP_TITLE_TOP & 0xFF))) * row / (title_h - 2));
            color = FB_RGB(r, g, b);
        }
        else
        {
            uint8_t r = ((COL_XP_TITLE_INACT_TOP >> 16) & 0xFF) +
                (((uint32_t)(((COL_XP_TITLE_INACT_BOTTOM >> 16) & 0xFF) - ((COL_XP_TITLE_INACT_TOP >> 16) & 0xFF))) * row / (title_h - 2));
            uint8_t g = ((COL_XP_TITLE_INACT_TOP >> 8) & 0xFF) +
                (((uint32_t)(((COL_XP_TITLE_INACT_BOTTOM >> 8) & 0xFF) - ((COL_XP_TITLE_INACT_TOP >> 8) & 0xFF))) * row / (title_h - 2));
            uint8_t b = (COL_XP_TITLE_INACT_TOP & 0xFF) +
                (((uint32_t)((COL_XP_TITLE_INACT_BOTTOM & 0xFF) - (COL_XP_TITLE_INACT_TOP & 0xFF))) * row / (title_h - 2));
            color = FB_RGB(r, g, b);
        }
        int indent = (row < 7) ? cr_mask[row] : 0;
        fb_draw_hline(y + row, x + indent, x + tw - 1 - indent, color);
    }

    fb_draw_hline(y + title_h - 1, x, x + tw - 1, active ? COL_XP_TITLE_BORDER : FB_RGB(0x90, 0x90, 0x90));

    int mmx = mouse_get_x_wrapper();
    int mmy = mouse_get_y_wrapper();
    int btn_sz = 20;
    int btn_top = y + (title_h - btn_sz) / 2;
    int btn_gap = 2;
    int btn_right = x + tw - 3;

    int close_x = btn_right - btn_sz;
    int max_x = close_x - btn_sz - btn_gap;
    int min_x = max_x - btn_sz - btn_gap;

    w->btn_close_hover = (mmx >= close_x && mmx < close_x + btn_sz &&
                          mmy >= btn_top && mmy < btn_top + btn_sz);
    w->btn_max_hover = (mmx >= max_x && mmx < max_x + btn_sz &&
                        mmy >= btn_top && mmy < btn_top + btn_sz);
    w->btn_min_hover = (mmx >= min_x && mmx < min_x + btn_sz &&
                        mmy >= btn_top && mmy < btn_top + btn_sz);

    int ver_label_y = y + (title_h - FONT_HEIGHT) / 2;

    {
        int icon_x = x + 4;
        int icon_y = y + 3;
        int icon_sz = 16;
        if (active)
        {
            fb_fillrect(icon_x, icon_y, icon_sz, icon_sz, COL_XP_TITLE_TEXT);
            fb_draw_rect_outline(icon_x, icon_y, icon_sz, icon_sz, FB_RGB(0x00, 0x20, 0x70));
            fb_fillrect(icon_x + 2, icon_y + 2, icon_sz - 4, icon_sz - 4, COL_XP_BTN_MAX_FACE);
            for (int i = 0; i < 4; i++)
                fb_draw_hline(icon_y + 5 + i * 3, icon_x + 4, icon_x + icon_sz - 5, FB_RGB(0x60, 0x90, 0xD0));
            draw_xp_title_text(icon_x + icon_sz + 4, ver_label_y, w->title, COL_XP_TITLE_TEXT, FB_RGB(0x00, 0x20, 0x70));
        }
        else
        {
            fb_fillrect(icon_x, icon_y, icon_sz, icon_sz, FB_RGB(0xA0, 0xB0, 0xD0));
            fb_draw_rect_outline(icon_x, icon_y, icon_sz, icon_sz, FB_RGB(0x60, 0x70, 0xA0));
            fb_fillrect(icon_x + 2, icon_y + 2, icon_sz - 4, icon_sz - 4, FB_RGB(0xC0, 0xCC, 0xE0));
            for (int i = 0; i < 4; i++)
                fb_draw_hline(icon_y + 5 + i * 3, icon_x + 4, icon_x + icon_sz - 5, FB_RGB(0x90, 0xA0, 0xC0));
            draw_xp_title_text(icon_x + icon_sz + 4, ver_label_y, w->title, COL_XP_TITLE_INACT_TEXT, 0);
        }
    }

    /* Close button — always drawn */
    {
        uint32_t border_col, face, hl;
        if (active)
        {
            face = w->btn_close_hover ? COL_XP_BTN_CLOSE_HOV : COL_XP_BTN_CLOSE_FACE;
            border_col = w->btn_close_hover ? FB_RGB(0xAA, 0x30, 0x00) : FB_RGB(0x88, 0x40, 0x20);
            hl = w->btn_close_hover ? FB_RGB(0xFF, 0x80, 0x40) : FB_RGB(0xDD, 0x70, 0x40);
        }
        else
        {
            face = FB_RGB(0xD0, 0xD0, 0xE0);
            border_col = FB_RGB(0x80, 0x80, 0xA0);
            hl = FB_RGB(0xE0, 0xE0, 0xF0);
        }
        fb_draw_rect_outline(close_x, btn_top, btn_sz, btn_sz, border_col);
        fb_fillrect(close_x + 1, btn_top + 1, btn_sz - 2, btn_sz - 2, face);
        fb_draw_hline(btn_top + 1, close_x + 1, close_x + btn_sz - 2, hl);
        fb_draw_vline(close_x + 1, btn_top + 1, btn_top + btn_sz - 2, hl);
        int cx2 = close_x + btn_sz / 2;
        int cy2 = btn_top + btn_sz / 2;
        uint32_t x_col = active ? COL_WHITE : FB_RGB(0x80, 0x80, 0xA0);
        for (int d = -5; d <= 5; d++)
        {
            fb_putpixel(cx2 + d, cy2 + d, x_col);
            fb_putpixel(cx2 + d, cy2 - d, x_col);
        }
    }

    if (!w->minimized)
    {
        /* Maximize button */
        {
            uint32_t face, border_col;
            if (active)
            {
                face = w->btn_max_hover ? COL_XP_BTN_HOVER : COL_XP_BTN_MAX_FACE;
                border_col = FB_RGB(0x30, 0x50, 0x90);
            }
            else
            {
                face = FB_RGB(0xC8, 0xD0, 0xE0);
                border_col = FB_RGB(0x70, 0x80, 0xA0);
            }
            fb_draw_rect_outline(max_x, btn_top, btn_sz, btn_sz, border_col);
            fb_fillrect(max_x + 1, btn_top + 1, btn_sz - 2, btn_sz - 2, face);
            if (active)
            {
                fb_draw_hline(btn_top + 1, max_x + 1, max_x + btn_sz - 2, FB_RGB(0x70, 0xA0, 0xE0));
                fb_draw_vline(max_x + 1, btn_top + 1, btn_top + btn_sz - 2, FB_RGB(0x70, 0xA0, 0xE0));
                fb_draw_hline(btn_top + btn_sz - 2, max_x + 2, max_x + btn_sz - 3, FB_RGB(0x20, 0x40, 0x80));
                fb_draw_vline(max_x + btn_sz - 2, btn_top + 2, btn_top + btn_sz - 3, FB_RGB(0x20, 0x40, 0x80));
            }
            int mx_cx = max_x + btn_sz / 2;
            int mx_cy = btn_top + btn_sz / 2;
            uint32_t g_col = active ? COL_WHITE : FB_RGB(0x80, 0x90, 0xB0);
            if (w->maximized)
            {
                fb_draw_rect_outline(mx_cx - 6, mx_cy - 5, 8, 7, g_col);
                fb_fillrect(mx_cx - 6, mx_cy - 5, 8, 2, g_col);
                fb_draw_rect_outline(mx_cx - 4, mx_cy - 7, 8, 7, g_col);
                fb_fillrect(mx_cx - 4, mx_cy - 7, 8, 2, g_col);
            }
            else
            {
                fb_draw_rect_outline(mx_cx - 5, mx_cy - 5, 10, 9, g_col);
                fb_fillrect(mx_cx - 5, mx_cy - 5, 10, 2, g_col);
            }
        }

        /* Minimize button */
        {
            uint32_t face, border_col;
            if (active)
            {
                face = w->btn_min_hover ? COL_XP_BTN_HOVER : COL_XP_BTN_MIN_FACE;
                border_col = FB_RGB(0x30, 0x50, 0x90);
            }
            else
            {
                face = FB_RGB(0xC8, 0xD0, 0xE0);
                border_col = FB_RGB(0x70, 0x80, 0xA0);
            }
            fb_draw_rect_outline(min_x, btn_top, btn_sz, btn_sz, border_col);
            fb_fillrect(min_x + 1, btn_top + 1, btn_sz - 2, btn_sz - 2, face);
            if (active)
            {
                fb_draw_hline(btn_top + 1, min_x + 1, min_x + btn_sz - 2, FB_RGB(0x70, 0xA0, 0xE0));
                fb_draw_vline(min_x + 1, btn_top + 1, btn_top + btn_sz - 2, FB_RGB(0x70, 0xA0, 0xE0));
                fb_draw_hline(btn_top + btn_sz - 2, min_x + 2, min_x + btn_sz - 3, FB_RGB(0x20, 0x40, 0x80));
                fb_draw_vline(min_x + btn_sz - 2, btn_top + 2, btn_top + btn_sz - 3, FB_RGB(0x20, 0x40, 0x80));
            }
            fb_fillrect(min_x + 5, btn_top + btn_sz - 6, btn_sz - 10, 2, active ? COL_WHITE : FB_RGB(0x80, 0x90, 0xB0));
        }
    }
}

static void draw_window_content(gui_window_t* w)
{
    int gf = XP_BORDER_W;
    int cx = w->x + gf;
    int cy = w->y + GUI_TITLE_HEIGHT + gf;
    int cw = w->w - gf * 2 - XP_SCROLLBAR_W;
    int ch = w->h - GUI_TITLE_HEIGHT - gf * 2 - 1;
    uint32_t fbw = fb_info.width;
    uint32_t fbh = fb_info.height;

    if (w->is_terminal)
    {
        fb_fillrect(cx, cy, cw, ch, FB_RGB(0x0C, 0x0C, 0x0C));
        fb_draw_hline(cy, cx + 1, cx + cw - 2, FB_RGB(0x1A, 0x1A, 0x2A));
        if (w->content)
        {
            int visible_rows = ch / FONT_HEIGHT;
            for (int row = 0; row < visible_rows; row++)
            {
                int buf_row = row + w->scroll_offset;
                if (buf_row >= w->ch) break;
                for (int col = 0; col < w->cw && col < cw / FONT_WIDTH; col++)
                {
                    char c = w->content[buf_row * w->cw + col];
                    if (c && c != ' ')
                        fb_drawchar(cx + col * FONT_WIDTH, cy + row * FONT_HEIGHT,
                                    c, FB_RGB(0xCC, 0xCC, 0xCC), FB_RGB(0x0C, 0x0C, 0x0C));
                }
            }
        }
    }
    else
    {
        fb_fillrect(cx, cy, cw, ch, COL_WHITE);
        if (w->pixels && w->pw > 0 && w->ph > 0)
        {
            uint32_t* bb = fb_get_backbuffer();
            if (bb)
            {
                uint32_t stride = fb_info.pitch / 4;
                int rows = w->ph < ch ? w->ph : ch;
                int cols = w->pw < cw ? w->pw : cw;
                for (int row = 0; row < rows; row++)
                {
                    uint32_t* src = &w->pixels[row * w->pw];
                    uint32_t* dst = &bb[(cy + row) * stride + cx];
                    for (int col = 0; col < cols; col++)
                    {
                        uint32_t color = src[col];
                        if (color != 0xFFFFFFFF)
                            dst[col] = color;
                    }
                }
            }
            else
            {
                for (int row = 0; row < w->ph && row < ch; row++)
                {
                    uint32_t* pix_row = &w->pixels[(uint32_t)(row * w->pw)];
                    for (int col = 0; col < w->pw && col < cw; col++)
                    {
                        uint32_t color = pix_row[col];
                        if (color != 0xFFFFFFFF)
                            fb_putpixel((uint32_t)(cx + col), (uint32_t)(cy + row), color);
                    }
                }
            }
        }
        if (w->content)
        {
            int visible_rows = ch / FONT_HEIGHT;
            for (int row = 0; row < visible_rows; row++)
            {
                int buf_row = row + w->scroll_offset;
                if (buf_row >= w->ch) break;
                for (int col = 0; col < w->cw && col < cw / FONT_WIDTH; col++)
                {
                    char c = w->content[buf_row * w->cw + col];
                    if (c)
                        fb_drawchar(cx + col * FONT_WIDTH, cy + row * FONT_HEIGHT,
                                    c, COL_BLACK, COL_WHITE);
                }
            }
        }
    }

    if (active_window >= 0 && &windows[active_window] == w)
    {
        int cur_x = cx + w->cursor_x * FONT_WIDTH;
        int cur_y = cy + (w->cursor_y - w->scroll_offset) * FONT_HEIGHT;
        if (cur_x >= cx && cur_x < cx + cw &&
            cur_y >= cy && cur_y < cy + ch &&
            w->cursor_y >= w->scroll_offset &&
            w->cursor_y < w->scroll_offset + ch / FONT_HEIGHT)
        {
            uint32_t* bb = fb_get_backbuffer();
            if (bb)
            {
                uint32_t stride = fb_info.pitch / 4;
                for (int row = 0; row < FONT_HEIGHT; row++)
                {
                    int py = cur_y + row;
                    if (py < 0 || (uint32_t)py >= fbh) continue;
                    for (int col = 0; col < FONT_WIDTH; col++)
                    {
                        int px = cur_x + col;
                        if (px < 0 || (uint32_t)px >= fbw) continue;
                        uint32_t c = bb[py * stride + px];
                        bb[py * stride + px] = ~c & 0x00FFFFFF;
                    }
                }
            }
        }
    }

    /* Draw vertical line separating scrollbar area */
    int sb_x = w->x + w->w - XP_BORDER_W - XP_SCROLLBAR_W;
    fb_draw_vline(sb_x, cy, cy + ch - 1, FB_RGB(0x80, 0x80, 0x80));

    /* Draw scrollbar */
    int sb_y = cy;
    int sb_h = ch;
    int visible_rows = ch / FONT_HEIGHT;
    int scroll_max = w->ch - visible_rows;
    if (scroll_max < 0) scroll_max = 0;
    w->scroll_max = scroll_max;
    if (w->scroll_offset > scroll_max) w->scroll_offset = scroll_max;
    if (w->scroll_offset < 0) w->scroll_offset = 0;
    draw_xp_scrollbar(sb_x + 1, sb_y, XP_SCROLLBAR_W - 1, sb_h,
                      w->scroll_offset, scroll_max, visible_rows);
}

static void draw_xp_scrollbar(int x, int y, int w, int h, int scroll_off, int scroll_max, int visible_rows)
{
    int btn_h = XP_SCROLLBAR_W;
    int track_h = h - btn_h * 2;
    if (track_h < 4) track_h = 4;

    uint32_t face = COL_XP_BTN_FACE;
    uint32_t border = COL_XP_BTN_BORDER;
    uint32_t arrow = FB_RGB(0x60, 0x60, 0x60);

    fb_fillrect(x, y, w, h, face);

    /* Up button */
    fb_draw_rect_outline(x, y, w, btn_h, border);
    fb_fillrect(x + 1, y + 1, w - 2, btn_h - 2, face);
    fb_draw_hline(y + 1, x + 2, x + w - 3, COL_WHITE);
    fb_draw_vline(x + 1, y + 2, y + btn_h - 3, COL_WHITE);
    int cx = x + w / 2;
    int cy = y + btn_h / 2;
    for (int r = 0; r < 3; r++)
    {
        int y_row = cy - 2 + r;
        for (int d = -r; d <= r; d++)
            fb_putpixel(cx + d, y_row, arrow);
    }

    /* Down button */
    int dy = y + h - btn_h;
    fb_draw_rect_outline(x, dy, w, btn_h, border);
    fb_fillrect(x + 1, dy + 1, w - 2, btn_h - 2, face);
    fb_draw_hline(dy + 1, x + 2, x + w - 3, COL_WHITE);
    fb_draw_vline(x + 1, dy + 2, dy + btn_h - 3, COL_WHITE);
    for (int r = 0; r < 3; r++)
    {
        int y_row = dy + btn_h - 1 - r;
        for (int d = -r; d <= r; d++)
            fb_putpixel(cx + d, y_row, arrow);
    }

    /* Track */
    uint32_t track_bg = FB_RGB(0xE8, 0xE8, 0xE8);
    fb_fillrect(x + 2, y + btn_h, w - 4, track_h, track_bg);

    /* Thumb */
    if (scroll_max > 0)
    {
        int thumb_h = track_h * visible_rows / (scroll_max + visible_rows);
        if (thumb_h < 12) thumb_h = 12;
        if (thumb_h > track_h - 2) thumb_h = track_h - 2;
        int thumb_y = y + btn_h + 2 + (track_h - 4 - thumb_h) * scroll_off / scroll_max;
        if (thumb_y < y + btn_h + 2) thumb_y = y + btn_h + 2;
        if (thumb_y + thumb_h > dy - 2) thumb_y = dy - 2 - thumb_h;

        fb_fillrect(x + 3, thumb_y, w - 6, thumb_h, FB_RGB(0xC0, 0xC8, 0xD8));
        fb_draw_rect_outline(x + 3, thumb_y, w - 6, thumb_h, FB_RGB(0x90, 0x98, 0xA8));
        fb_draw_hline(thumb_y + 1, x + 5, x + w - 8, FB_RGB(0xE0, 0xE8, 0xF0));
        fb_draw_vline(x + 4, thumb_y + 2, thumb_y + thumb_h - 3, FB_RGB(0xD0, 0xD8, 0xE8));
        fb_draw_hline(thumb_y + thumb_h - 2, x + 5, x + w - 8, FB_RGB(0xA0, 0xA8, 0xB8));
        fb_draw_vline(x + w - 7, thumb_y + 2, thumb_y + thumb_h - 3, FB_RGB(0xA0, 0xA8, 0xB8));
    }
}

static void draw_window(int idx)
{
    gui_window_t* w = &windows[idx];
    if (!w->visible || w->minimized) return;
    if (w->dragging) { w->x = w->drag_outline_x; w->y = w->drag_outline_y; }

    int active = (idx == active_window);
    int x = w->x, y = w->y, ww = w->w, wh = w->h;
    int gf = XP_BORDER_W;

    int cx = x + gf;
    int cy = y + GUI_TITLE_HEIGHT + gf;
    int cw = ww - gf * 2;
    int ch = wh - GUI_TITLE_HEIGHT - gf * 2;

    draw_xp_title_bar(w, active);

    uint32_t border = active ? COL_XP_WINDOW_BORDER_ACTIVE : COL_XP_WINDOW_BORDER_INACT;

    int cr = 6;
    fb_draw_hline(y, x + cr, x + ww - 1 - cr, border);
    fb_draw_hline(y + wh - 1, x + cr, x + ww - 1 - cr, border);
    fb_draw_vline(x, y + cr, y + wh - 1 - cr, border);
    fb_draw_vline(x + ww - 1, y + cr, y + wh - 1 - cr, border);
    for (int ci = 0; ci < cr - 1; ci++)
    {
        int cj = cr - 1 - ci;
        fb_putpixel(x + cj, y + 1 + ci, border);
        fb_putpixel(x + ww - 1 - cj, y + 1 + ci, border);
        fb_putpixel(x + cj, y + wh - 2 - ci, border);
        fb_putpixel(x + ww - 1 - cj, y + wh - 2 - ci, border);
    }

    fb_fillrect(cx, cy, cw, ch, COL_WHITE);
    if (!w->is_terminal)
    {
        /* Inner border with rounded bottom corners */
        int icr = 3;
        fb_draw_hline(cy, cx + icr, cx + cw - 1 - icr, FB_RGB(0x80, 0x80, 0x80));
        fb_draw_hline(cy + ch - 1, cx + icr, cx + cw - 1 - icr, FB_RGB(0x80, 0x80, 0x80));
        fb_draw_vline(cx, cy + icr, cy + ch - 1 - icr, FB_RGB(0x80, 0x80, 0x80));
        fb_draw_vline(cx + cw - 1, cy + icr, cy + ch - 1 - icr, FB_RGB(0x80, 0x80, 0x80));

        fb_draw_hline(cy + 1, cx + 1 + icr, cx + cw - 2 - icr, COL_WHITE);
        fb_draw_vline(cx + 1, cy + 1 + icr, cy + ch - 2 - icr, COL_WHITE);
    }

    draw_window_content(w);

    for (int b = 0; b < w->num_buttons; b++)
    {
        gui_button_t* btn = &w->buttons[b];
        int bx = x + XP_BORDER_W + btn->x * FONT_WIDTH;
        int by = y + GUI_TITLE_HEIGHT + XP_BORDER_W + btn->y * FONT_HEIGHT;
        int bw = btn->w * FONT_WIDTH;
        int bh = FONT_HEIGHT + 4;

        draw_xp_button_3d(bx, by, bw, bh, btn->label, COL_XP_BTN_FACE, 0);
    }
}

static void draw_mouse_cursor(void)
{
    if (!mouse_is_present_wrapper()) return;
    int mx = mouse_get_x_wrapper();
    int my = mouse_get_y_wrapper();
    if (mx < 0 || (uint32_t)mx >= fb_info.width || my < 0 || (uint32_t)my >= fb_info.height) return;

    static const unsigned char cursor_data[] = {
        0xFF, 0xC0, 0xFF, 0xE0, 0xFF, 0xE0, 0xFF, 0xF0,
        0xFF, 0xF0, 0xFF, 0xF8, 0xFF, 0xF8, 0xFF, 0xFC,
        0x67, 0xFC, 0x07, 0xFE, 0x03, 0xFE, 0x01, 0xFF,
        0x00, 0xFF, 0x00, 0x7E, 0x00, 0x3C,
    };

    for (int row = 0; row < 15; row++)
    {
        int py = my + row + 1;
        if (py < 0 || (uint32_t)py >= fb_info.height) continue;
        for (int col = 0; col < 11; col++)
        {
            int px = mx + col + 1;
            if (px < 0 || (uint32_t)px >= fb_info.width) continue;
            if (cursor_data[row] & (1 << (10 - col)))
                fb_putpixel(px, py, 0x00404040);
        }
    }

    for (int row = 0; row < 15; row++)
    {
        int py = my + row;
        if (py < 0 || (uint32_t)py >= fb_info.height) continue;
        for (int col = 0; col < 11; col++)
        {
            int px = mx + col;
            if (px < 0 || (uint32_t)px >= fb_info.width) continue;
            if (cursor_data[row] & (1 << (10 - col)))
            {
                uint32_t bg = fb_getpixel(px, py);
                uint32_t max = FB_GET_R(bg);
                uint32_t g = FB_GET_G(bg);
                uint32_t b = FB_GET_B(bg);
                if (g > max) max = g;
                if (b > max) max = b;
                fb_putpixel(px, py, max > 128 ? COL_BLACK : COL_WHITE);
            }
        }
    }
}

static void draw_start_button(int x, int y, int w, int h, int hovered)
{
    uint32_t top = hovered ? FB_RGB(0x50, 0xC0, 0x10) : COL_XP_START_GREEN;
    uint32_t bottom = hovered ? FB_RGB(0x30, 0x80, 0x00) : FB_RGB(0x28, 0x70, 0x00);

    int pill_r = 5;

    for (int row = 0; row < h; row++)
    {
        int top_dist = row;
        int bot_dist = h - 1 - row;
        int indent = 0;
        if (top_dist < pill_r)
            indent = pill_r - 1 - top_dist;
        else if (bot_dist < pill_r)
            indent = pill_r - 1 - bot_dist;
        int x0 = x + indent;
        int x1 = x + w - 1 - indent;

        uint8_t r = ((top >> 16) & 0xFF) +
            (((uint32_t)(((bottom >> 16) & 0xFF) - ((top >> 16) & 0xFF))) * row / (h - 1));
        uint8_t g = ((top >> 8) & 0xFF) +
            (((uint32_t)(((bottom >> 8) & 0xFF) - ((top >> 8) & 0xFF))) * row / (h - 1));
        uint8_t b = (top & 0xFF) +
            (((uint32_t)((bottom & 0xFF) - (top & 0xFF))) * row / (h - 1));
        fb_draw_hline(y + row, x0, x1, FB_RGB(r, g, b));
    }

    /* 3D edge highlights following the pill curve */
    for (int d = 0; d < pill_r; d++)
    {
        int top_indent = pill_r - 1 - d;
        fb_putpixel(x + top_indent, y + d, FB_RGB(0x70, 0xE0, 0x30));
        fb_putpixel(x + w - 1 - top_indent, y + d, FB_RGB(0x18, 0x50, 0x00));
        int bot_row = h - 1 - d;
        fb_putpixel(x + top_indent, y + bot_row, FB_RGB(0x18, 0x50, 0x00));
        fb_putpixel(x + w - 1 - top_indent, y + bot_row, FB_RGB(0x70, 0xE0, 0x30));
    }
    fb_draw_hline(y + pill_r, x, x + w - 1, FB_RGB(0x70, 0xE0, 0x30));
    fb_draw_vline(x, y + pill_r, y + h - 1 - pill_r, FB_RGB(0x70, 0xE0, 0x30));
    fb_draw_hline(y + h - 1 - pill_r, x, x + w - 1, FB_RGB(0x18, 0x50, 0x00));
    fb_draw_vline(x + w - 1, y + pill_r, y + h - 1 - pill_r, FB_RGB(0x18, 0x50, 0x00));

    /* XP Windows flag logo */
    int lx = x + 8;
    int ly = y + (h - 16) / 2;
    fb_fillrect(lx, ly, 8, 8, FB_RGB(0xE8, 0x50, 0x50));
    fb_fillrect(lx + 8, ly, 8, 8, FB_RGB(0x50, 0xB0, 0x50));
    fb_fillrect(lx, ly + 8, 8, 8, FB_RGB(0x50, 0x80, 0xE8));
    fb_fillrect(lx + 8, ly + 8, 8, 8, FB_RGB(0xE8, 0xC8, 0x20));
    fb_draw_rect_outline(lx, ly, 16, 16, COL_WHITE);

    draw_xp_title_text(lx + 20, y + (h - FONT_HEIGHT) / 2, "start", COL_WHITE, 0);
}

static void draw_start_menu(void)
{
    if (!start_menu_open) return;

    int tby = fb_info.height - XP_TASKBAR_H;
    int max_items = start_left_count > start_right_count ? start_left_count : start_right_count;
    int total_h = XP_SM_HEADER_H + max_items * XP_SM_ITEM_H + XP_SM_BOTTOM_H;
    int mx = 0;
    int my = tby - total_h;
    int mw = XP_SM_TOTAL_W;

    /* Shadow */
    for (int i = 1; i <= 4; i++)
    {
        uint8_t a = 30 - (uint32_t)(i * 25) / 4;
        fb_fillrect_alpha(mx + i, my + i, mw, total_h, COL_BLACK, a);
    }

    /* Menu border */
    fb_draw_rect_outline(mx, my, mw, total_h, FB_RGB(0x55, 0x55, 0x55));

    /* Header - reversed gradient (dark top → light bottom, opposite of title bar) */
    int hh = XP_SM_HEADER_H;
    for (int row = 0; row < hh; row++)
    {
        uint8_t r = ((COL_XP_TITLE_BOTTOM >> 16) & 0xFF) +
            (((uint32_t)(((COL_XP_TITLE_TOP >> 16) & 0xFF) - ((COL_XP_TITLE_BOTTOM >> 16) & 0xFF))) * row / (hh - 1 < 1 ? 1 : hh - 1));
        uint8_t g = ((COL_XP_TITLE_BOTTOM >> 8) & 0xFF) +
            (((uint32_t)(((COL_XP_TITLE_TOP >> 8) & 0xFF) - ((COL_XP_TITLE_BOTTOM >> 8) & 0xFF))) * row / (hh - 1 < 1 ? 1 : hh - 1));
        uint8_t b = (COL_XP_TITLE_BOTTOM & 0xFF) +
            (((uint32_t)((COL_XP_TITLE_TOP & 0xFF) - (COL_XP_TITLE_BOTTOM & 0xFF))) * row / (hh - 1 < 1 ? 1 : hh - 1));
        fb_draw_hline(my + row, mx, mx + mw - 1, FB_RGB(r, g, b));
    }

    /* Header bottom border */
    fb_draw_hline(my + hh - 1, mx, mx + mw - 1, FB_RGB(0x00, 0x30, 0x92));

    /* User picture */
    int icon_sz = 48;
    int icon_x = mx + 8;
    int icon_y = my + (hh - icon_sz) / 2;
    fb_fillrect(icon_x, icon_y, icon_sz, icon_sz, FB_RGB(0xE0, 0xE0, 0xE0));
    fb_draw_rect_outline(icon_x, icon_y, icon_sz, icon_sz, FB_RGB(0x00, 0x30, 0x92));
    int ic_half = icon_sz / 2;
    int ic_radius = ic_half - 2;
    for (int row = 0; row < icon_sz; row++)
        for (int col = 0; col < icon_sz; col++)
        {
            int dx = col - ic_half, dy = row - ic_half;
            if (dx * dx + dy * dy <= ic_radius * ic_radius)
            {
                int v = 0xD0 - (dx * dx + dy * dy) / 20;
                if (v < 0xA0) v = 0xA0;
                fb_putpixel(icon_x + col, icon_y + row, FB_RGB(v, v - 20, v - 40));
            }
        }
    fb_drawstring(icon_x + ic_half - 4, icon_y + ic_half - 4, "U", FB_RGB(0x60, 0x60, 0x80), 0);
    draw_xp_title_text(icon_x + icon_sz + 8, my + hh / 2 - FONT_HEIGHT / 2, "Default User", COL_WHITE, FB_RGB(0x00, 0x20, 0x60));

    /* Left column */
    int left_x = mx + 1;
    int left_y = my + hh;
    int left_w = XP_SM_LEFT_W - 1;
    int left_h = max_items * XP_SM_ITEM_H;
    int right_x = left_x + left_w + 1;
    int right_w = XP_SM_RIGHT_W - 1;

    fb_fillrect(left_x, left_y, left_w, left_h, COL_XP_SM_LEFT_BG);
    fb_fillrect(right_x, left_y, right_w, left_h, COL_XP_SM_RIGHT_BG);

    /* Vertical separator between columns */
    fb_draw_vline(right_x - 1, left_y, left_y + left_h - 1, FB_RGB(0xC0, 0xC0, 0xC0));

    for (int i = 0; i < start_left_count; i++)
    {
        int iy = left_y + i * XP_SM_ITEM_H;
        uint32_t bg = (start_menu_hovered == i) ? COL_XP_HIGHLIGHT : COL_XP_SM_LEFT_BG;
        uint32_t fg = (start_menu_hovered == i) ? COL_WHITE : COL_BLACK;
        fb_fillrect(left_x + 2, iy, left_w - 4, XP_SM_ITEM_H, bg);
        fb_fillrect(left_x + 6, iy + 6, 16, 16, FB_RGB(0x30, 0x80, 0xD0));
        fb_draw_rect_outline(left_x + 6, iy + 6, 16, 16, FB_RGB(0x20, 0x60, 0xB0));
        fb_drawstring(left_x + 28, iy + (XP_SM_ITEM_H - FONT_HEIGHT) / 2, start_left_items[i], fg, bg);
    }

    for (int i = 0; i < start_right_count; i++)
    {
        int iy = left_y + i * XP_SM_ITEM_H;
        int adj_idx = start_left_count + i;
        uint32_t bg = (start_menu_hovered == adj_idx) ? COL_XP_HIGHLIGHT : COL_XP_SM_RIGHT_BG;
        uint32_t fg = (start_menu_hovered == adj_idx) ? COL_WHITE : COL_BLACK;
        fb_fillrect(right_x + 2, iy, right_w - 4, XP_SM_ITEM_H, bg);
        fb_drawstring(right_x + 8, iy + 6, start_right_items[i], fg, bg);
    }

    /* Bottom bar */
    int bottom_y = left_y + left_h;
    fb_fillrect(mx, bottom_y, mw, XP_SM_BOTTOM_H, COL_XP_SM_BOTTOM_BG);
    fb_draw_hline(bottom_y, mx, mx + mw - 1, FB_RGB(0xB0, 0xB0, 0xB0));

    int shutdown_idx = start_left_count + start_right_count;
    int shutdown_x = mw - 100;
    int shutdown_w = 94;
    int btn_h = 22;
    int btn_y = bottom_y + (XP_SM_BOTTOM_H - btn_h) / 2;

    /* Shut Down button */
    uint32_t s_bg = (start_menu_hovered == shutdown_idx) ? COL_XP_HIGHLIGHT : COL_XP_SM_BOTTOM_BG;
    uint32_t s_fg = (start_menu_hovered == shutdown_idx) ? COL_WHITE : COL_BLACK;
    fb_draw_rect_outline(shutdown_x, btn_y, shutdown_w, btn_h, FB_RGB(0x80, 0x80, 0x80));
    fb_fillrect(shutdown_x + 1, btn_y + 1, shutdown_w - 2, btn_h - 2, s_bg);
    fb_drawstring(shutdown_x + 6, btn_y + 4, "Shut Down", s_fg, s_bg);

    /* Log Off button */
    int logoff_idx = start_left_count + start_right_count + 1;
    int logoff_x = shutdown_x - 88;
    uint32_t l_bg = (start_menu_hovered == logoff_idx) ? COL_XP_HIGHLIGHT : COL_XP_SM_BOTTOM_BG;
    uint32_t l_fg = (start_menu_hovered == logoff_idx) ? COL_WHITE : COL_BLACK;
    fb_draw_rect_outline(logoff_x, btn_y, 84, btn_h, FB_RGB(0x80, 0x80, 0x80));
    fb_fillrect(logoff_x + 1, btn_y + 1, 82, btn_h - 2, l_bg);
    fb_drawstring(logoff_x + 6, btn_y + 4, "Log Off", l_fg, l_bg);
}

static void draw_taskbar(void)
{
    int tby = fb_info.height - XP_TASKBAR_H;
    int tw = (int)fb_info.width;

    uint32_t top = FB_RGB(0x24, 0x5E, 0xDC);
    uint32_t bottom = FB_RGB(0x18, 0x40, 0xB0);
    for (int row = 0; row < XP_TASKBAR_H; row++)
    {
        uint8_t r = ((top >> 16) & 0xFF) +
            (((uint32_t)(((bottom >> 16) & 0xFF) - ((top >> 16) & 0xFF))) * row / (XP_TASKBAR_H - 1 < 1 ? 1 : XP_TASKBAR_H - 1));
        uint8_t g = ((top >> 8) & 0xFF) +
            (((uint32_t)(((bottom >> 8) & 0xFF) - ((top >> 8) & 0xFF))) * row / (XP_TASKBAR_H - 1 < 1 ? 1 : XP_TASKBAR_H - 1));
        uint8_t b = (top & 0xFF) +
            (((uint32_t)((bottom & 0xFF) - (top & 0xFF))) * row / (XP_TASKBAR_H - 1 < 1 ? 1 : XP_TASKBAR_H - 1));
        fb_draw_hline(tby + row, 0, tw - 1, FB_RGB(r, g, b));
    }
    fb_draw_hline(tby, 0, tw - 1, FB_RGB(0x60, 0x90, 0xF0));

    int start_x = 2;
    int start_w = XP_START_W;
    int start_y = tby + 2;
    int start_h = XP_TASKBAR_H - 4;
    int mx = mouse_get_x_wrapper();
    int my = mouse_get_y_wrapper();
    int start_hovered = (mx >= start_x && mx < start_x + start_w &&
                         my >= start_y && my < start_y + start_h);
    draw_start_button(start_x, start_y, start_w, start_h, start_hovered);

    int bx = start_x + start_w + 4;

    int tray_x = tw - XP_TRAY_W;
    int max_bw = tray_x - bx - 4;

    for (int i = 0; i < num_windows; i++)
    {
        if (!windows[i].visible) continue;
        int bw = strlen(windows[i].title) * FONT_WIDTH + 24;
        if (bw > 160) bw = 160;
        if (bx + bw > max_bw)
        {
            int remaining = max_bw - bx;
            if (remaining < 40) break;
            bw = remaining;
        }
        int is_active = (i == active_window) && !windows[i].minimized;
        int btn_y = tby + 2;
        int btn_h = XP_TASKBAR_H - 4;

        if (is_active)
        {
            fb_fillrect(bx, btn_y, bw, btn_h, FB_RGB(0xE8, 0xEE, 0xFA));
            fb_draw_rect_outline(bx, btn_y, bw, btn_h, FB_RGB(0x70, 0x88, 0xB8));
            fb_draw_hline(btn_y + 1, bx + 1, bx + bw - 2, FB_RGB(0xF4, 0xF8, 0xFF));
            fb_draw_vline(bx + 1, btn_y + 1, btn_y + btn_h - 2, FB_RGB(0xF4, 0xF8, 0xFF));
            fb_draw_hline(btn_y + btn_h - 2, bx + 1, bx + bw - 2, FB_RGB(0xA8, 0xBA, 0xD0));
            fb_draw_vline(bx + bw - 2, btn_y + 1, btn_y + btn_h - 2, FB_RGB(0xA8, 0xBA, 0xD0));
            fb_drawstring(bx + 8, btn_y + (btn_h - FONT_HEIGHT) / 2 + 1, windows[i].title, COL_BLACK, FB_RGB(0xE8, 0xEE, 0xFA));
        }
        else
        {
            fb_draw_rect_outline(bx, btn_y, bw, btn_h, FB_RGB(0x28, 0x50, 0xA8));
            fb_draw_hline(btn_y + 1, bx + 1, bx + bw - 2, FB_RGB(0x50, 0x80, 0xE0));
            fb_draw_vline(bx + 1, btn_y + 1, btn_y + btn_h - 2, FB_RGB(0x50, 0x80, 0xE0));
            fb_fillrect(bx + 2, btn_y + 2, bw - 4, btn_h - 4, FB_RGB(0x34, 0x64, 0xD4));
            fb_draw_hline(btn_y + btn_h - 2, bx + 2, bx + bw - 3, FB_RGB(0x1C, 0x3C, 0x98));
            fb_draw_vline(bx + bw - 2, btn_y + 2, btn_y + btn_h - 3, FB_RGB(0x1C, 0x3C, 0x98));
            fb_drawstring(bx + 8, btn_y + (btn_h - FONT_HEIGHT) / 2 + 1, windows[i].title, COL_WHITE, FB_RGB(0x34, 0x64, 0xD4));
        }
        bx += bw + 3;
    }

    fb_draw_vline(tray_x, tby + 4, tby + XP_TASKBAR_H - 5, FB_RGB(0x40, 0x70, 0xD0));

    uint8_t rtc_h = rtc_get_hours();
    uint8_t rtc_m = rtc_get_minutes();
    int pm = rtc_h >= 12;
    if (rtc_h > 12) rtc_h -= 12;
    if (rtc_h == 0) rtc_h = 12;
    char time_buf[16];
    time_buf[0] = '0' + rtc_h / 10;
    time_buf[1] = '0' + rtc_h % 10;
    time_buf[2] = ':';
    time_buf[3] = '0' + rtc_m / 10;
    time_buf[4] = '0' + rtc_m % 10;
    time_buf[5] = ' ';
    time_buf[6] = pm ? 'P' : 'A';
    time_buf[7] = 'M';
    time_buf[8] = 0;
    int time_x = tray_x + (XP_TRAY_W - 8 - 8 * FONT_WIDTH) / 2;
    fb_drawstring(time_x, tby + (XP_TASKBAR_H - FONT_HEIGHT) / 2, time_buf, COL_WHITE, 0);

    int sdb_x = tw - XP_SHOWDESKTOP_W;
    fb_fillrect(sdb_x, tby, XP_SHOWDESKTOP_W, XP_TASKBAR_H, FB_RGB(0x20, 0x48, 0xA0));
    fb_draw_vline(sdb_x, tby + 2, tby + XP_TASKBAR_H - 3, FB_RGB(0x50, 0x80, 0xE0));
}

static void handle_start_menu_click(int mx, int my)
{
    if (!start_menu_open) return;
    int tby = fb_info.height - XP_TASKBAR_H;
    int max_items = start_left_count > start_right_count ? start_left_count : start_right_count;
    int total_h = XP_SM_HEADER_H + max_items * XP_SM_ITEM_H + XP_SM_BOTTOM_H;
    int smx = 0;
    int smy = tby - total_h;

    if (mx < smx || mx >= smx + XP_SM_TOTAL_W || my < smy || my >= smy + total_h)
    {
        start_menu_open = 0;
        mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
        return;
    }
    if (my < smy + XP_SM_HEADER_H) return;

    int item_area_y = smy + XP_SM_HEADER_H;
    int bottom_y = smy + XP_SM_HEADER_H + max_items * XP_SM_ITEM_H;

    if (my >= bottom_y)
    {
        int shutdown_x = smx + XP_SM_TOTAL_W - 100;
        int logoff_x = shutdown_x - 88;
        if (mx >= shutdown_x && mx < shutdown_x + 94)
        {
            start_menu_open = 0;
            mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
            cmd_should_exit = 1;
        }
        else if (mx >= logoff_x && mx < logoff_x + 84)
        {
            start_menu_open = 0;
            mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
        }
        return;
    }

    int sep_x = XP_SM_LEFT_W;
    int idx = (my - item_area_y) / XP_SM_ITEM_H;
    if (idx < 0) return;

    if (mx < sep_x)
    {
        if (idx < start_left_count)
        {
            start_menu_open = 0;
            mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
            if (start_left_paths[idx])
                blu_spawn(start_left_paths[idx]);
        }
    }
    else
    {
        if (idx >= start_right_count) return;
        start_menu_open = 0;
        mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
        if (strcmp(start_right_items[idx], "Help & Support") == 0)
        {
            if (gui_terminal_win >= 0)
                gui_puts(gui_terminal_win, "\nBlueOS Help\n  Start menu for programs\n  Window minimize/close\n\n");
        }
        else if (strcmp(start_right_items[idx], "Computer") == 0)
        {
            printf("\n=== BlueOS System Information ===\n");
            printf("  Resolution: %dx%d %dbpp\n", fb_info.width, fb_info.height, fb_info.bpp);
            printf("  Heap: %d KB used / %d KB free\n",
                   mem_get_used() / 1024, mem_get_free() / 1024);
            printf("  Frames: %d used / %d total\n",
                   paging_get_used_frames(), paging_get_total_frames());
            int pcount = process_get_count();
            printf("  Processes: %d\n", pcount);
            if (gui_terminal_win >= 0)
                gui_puts(gui_terminal_win, "\nSystem info printed to serial.\n\n");
        }
    }
}

static int handle_taskbar_click(int mx, int my)
{
    int tby = fb_info.height - XP_TASKBAR_H;
    if (my < tby || (uint32_t)my >= fb_info.height) return 0;

    int start_x = 2;
    int start_w = XP_START_W;
    int start_y = tby + 2;
    int start_h = XP_TASKBAR_H - 4;
    if (mx >= start_x && mx < start_x + start_w &&
        my >= start_y && my < start_y + start_h)
    {
        start_menu_open = !start_menu_open;
        mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
        return 1;
    }

    int bx = start_x + start_w + 4;
    int tray_x = (int)fb_info.width - XP_TRAY_W;
    for (int i = 0; i < num_windows; i++)
    {
        if (!windows[i].visible) continue;
        int bw = strlen(windows[i].title) * FONT_WIDTH + 24;
        if (bw > 160) bw = 160;
        if (bx + bw > tray_x - 4)
        {
            int remaining = tray_x - 4 - bx;
            if (remaining < 40) break;
            bw = remaining;
        }
        if (mx >= bx && mx < bx + bw)
        {
            if (windows[i].minimized)
            {
                windows[i].minimized = 0;
                bring_to_front(i);
            }
            else if (active_window != i)
                bring_to_front(i);
            else
                windows[i].minimized = 1;
            return 1;
        }
        bx += bw + 3;
    }

    int sdb_x = (int)fb_info.width - XP_SHOWDESKTOP_W;
    if (mx >= sdb_x)
    {
        int any_visible = 0;
        for (int i = 0; i < num_windows; i++)
        {
            if (windows[i].visible && !windows[i].minimized)
            {
                windows[i].minimized = 1;
                any_visible = 1;
            }
        }
        if (!any_visible)
        {
            for (int i = 0; i < num_windows; i++)
                if (windows[i].visible) windows[i].minimized = 0;
        }
        return 1;
    }
    return 0;
}

static void handle_click(void)
{
    int mx = mouse_get_x_wrapper();
    int my = mouse_get_y_wrapper();
    if (mx < 0 || (uint32_t)mx >= fb_info.width || my < 0 || (uint32_t)my >= fb_info.height)
        return;

    if (start_menu_open)
    {
        int tby = fb_info.height - XP_TASKBAR_H;
        int max_items = start_left_count > start_right_count ? start_left_count : start_right_count;
        int total_h = XP_SM_HEADER_H + max_items * XP_SM_ITEM_H + XP_SM_BOTTOM_H;
        int smy = tby - total_h;
        int in_menu = (mx >= 0 && mx < XP_SM_TOTAL_W && my >= smy && my < smy + total_h);
        if (in_menu) { handle_start_menu_click(mx, my); return; }

        int start_x = 2;
        int start_w = XP_START_W;
        if (my >= tby && mx >= start_x && mx < start_x + start_w) { start_menu_open = 0; mark_screen_dirty(0, 0, fb_info.width, fb_info.height); return; }
        start_menu_open = 0; mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
    }

    if (handle_taskbar_click(mx, my)) return;

    for (int i = 0; i < num_desktop_icons; i++)
    {
        desktop_icon_t* di = &desktop_icons[i];
        if (mx >= di->x && mx < di->x + di->w && my >= di->y && my < di->y + di->h)
        {
            blu_spawn(di->path);
            return;
        }
    }

    for (int i = num_windows - 1; i >= 0; i--)
    {
        gui_window_t* w = &windows[i];
        if (!w->visible || w->minimized) continue;
        if (mx < w->x || mx >= w->x + w->w || my < w->y || my >= w->y + w->h) continue;

        int title_h = GUI_TITLE_HEIGHT;
        int btn_sz = 18;
        int btn_top = w->y + (title_h - btn_sz) / 2;
        int btn_gap = 2;
        int btn_right = w->x + w->w - 3;
        int close_x = btn_right - btn_sz;
        int max_x = close_x - btn_sz - btn_gap;
        int min_x = max_x - btn_sz - btn_gap;

        if (my >= btn_top && my < btn_top + btn_sz)
        {
            if (mx >= close_x && mx < close_x + btn_sz)
            {
                w->visible = 0;
                if (active_window == i) active_window = -1;
                return;
            }
            if (mx >= min_x && mx < min_x + btn_sz)
            {
                w->minimized = 1;
                if (active_window == i) active_window = -1;
                return;
            }
            if (mx >= max_x && mx < max_x + btn_sz)
            {
                if (w->maximized)
                {
                    w->x = w->restore_x; w->y = w->restore_y;
                    w->w = w->restore_w; w->h = w->restore_h;
                    w->maximized = 0;
                }
                else
                {
                    w->restore_x = w->x; w->restore_y = w->y;
                    w->restore_w = w->w; w->restore_h = w->h;
                    w->x = 0; w->y = 0;
                    w->w = fb_info.width;
                    w->h = fb_info.height - XP_TASKBAR_H - GUI_TITLE_HEIGHT - 2;
                    w->maximized = 1;
                }
                return;
            }
        }

        if (i != active_window) { bring_to_front(i); w = &windows[active_window]; }

        int edge_l = mx - w->x <= GUI_RESIZE_BORDER;
        int edge_r = w->x + w->w - 1 - mx <= GUI_RESIZE_BORDER;
        int edge_t = my - w->y <= GUI_RESIZE_BORDER;
        int edge_b = w->y + w->h - 1 - my <= GUI_RESIZE_BORDER;
        int edge = (edge_l ? 1 : 0) | (edge_r ? 2 : 0) | (edge_t ? 4 : 0) | (edge_b ? 8 : 0);

        if (edge && !w->maximized)
        {
            w->resizing = 1;
            w->resize_edge = edge;
            w->drag_off_x = mx;
            w->drag_off_y = my;
            return;
        }

        if (my >= w->y && my < w->y + title_h && !w->maximized)
        {
            w->dragging = 1;
            w->drag_off_x = mx - w->x;
            w->drag_off_y = my - w->y;
            w->drag_outline_x = w->x;
            w->drag_outline_y = w->y;
            return;
        }

    int cx = mx - (w->x + XP_BORDER_W);
    int cy = my - (w->y + title_h + XP_BORDER_W);
    for (int b = 0; b < w->num_buttons; b++)
    {
        gui_button_t* btn = &w->buttons[b];
        int bx = btn->x * FONT_WIDTH;
        int by = btn->y * FONT_HEIGHT;
        if (cx >= bx && cx < bx + btn->w * FONT_WIDTH && cy >= by && cy < by + FONT_HEIGHT + 4)
            {
                if (btn->on_click) btn->on_click(i, b);
                return;
            }
        }

        /* Scrollbar click handling */
        {
            int sb_x = w->x + w->w - XP_BORDER_W - XP_SCROLLBAR_W;
            int title_h = GUI_TITLE_HEIGHT;
            int sb_y = w->y + title_h + XP_BORDER_W;
            int sb_h = w->h - title_h - XP_BORDER_W * 2 - 1;
            int visible_rows = sb_h / FONT_HEIGHT;
            int scroll_max = w->ch - visible_rows;
            if (scroll_max < 0) scroll_max = 0;

            if (mx >= sb_x && mx < sb_x + XP_SCROLLBAR_W && my >= sb_y && my < sb_y + sb_h)
            {
                int btn_h = XP_SCROLLBAR_W;
                int track_h = sb_h - btn_h * 2;
                if (my < sb_y + btn_h)
                {
                    if (w->scroll_offset > 0) w->scroll_offset--;
                    return;
                }
                if (my >= sb_y + sb_h - btn_h)
                {
                    if (w->scroll_offset < scroll_max) w->scroll_offset++;
                    return;
                }
                if (scroll_max > 0)
                {
                    int thumb_h = track_h * visible_rows / (scroll_max + visible_rows);
                    if (thumb_h < 12) thumb_h = 12;
                    if (thumb_h > track_h - 2) thumb_h = track_h - 2;
                    w->scroll_offset = (my - sb_y - btn_h - thumb_h / 2) * scroll_max / track_h;
                    if (w->scroll_offset < 0) w->scroll_offset = 0;
                    if (w->scroll_offset > scroll_max) w->scroll_offset = scroll_max;
                }
                return;
            }
        }

        if (w->on_content_click) { w->on_content_click(i, mx, my); return; }
        {
            int nx = w->event_tail + 1;
            if (nx == GUI_EVENT_QUEUE_SIZE) nx = 0;
            if (nx != w->event_head)
            {
                w->event_queue[w->event_tail].type = 1;
                w->event_queue[w->event_tail].mx = mx;
                w->event_queue[w->event_tail].my = my;
                w->event_queue[w->event_tail].buttons = mouse_get_buttons_wrapper();
                w->event_tail = nx;
            }
        }
        return;
    }
}

static void gui_ensure_pixels(int win_id)
{
    if (win_id < 0 || win_id >= num_windows) return;
    gui_window_t* w = &windows[win_id];
    int new_pw = w->w - XP_BORDER_W * 2;
    int new_ph = w->h - GUI_TITLE_HEIGHT - XP_BORDER_W * 2 - 1;
    if (new_pw < 1) new_pw = 1;
    if (new_ph < 1) new_ph = 1;
    if (w->pixels)
    {
        if (w->pw == new_pw && w->ph == new_ph)
            return;
        if (!w->pixels_page_allocated)
            free(w->pixels);
        w->pixels = NULL;
    }
    w->pw = new_pw;
    w->ph = new_ph;
    gui_pixel_alloc(w);
}

void gui_draw_rect(int win_id, int x, int y, int w, int h, uint32_t color)
{
    if (win_id < 0 || win_id >= num_windows) return;
    gui_ensure_pixels(win_id);
    gui_window_t* win = &windows[win_id];
    if (!win->pixels) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > win->pw) w = win->pw - x;
    if (y + h > win->ph) h = win->ph - y;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++)
    {
        uint32_t* line = &win->pixels[(uint32_t)((y + row) * win->pw + x)];
        for (int col = 0; col < w; col++)
            line[col] = color;
    }
    mark_window_dirty(win_id, x, y, w, h);
}

void gui_draw_text(int win_id, int x, int y, const char* str, uint32_t fg, uint32_t bg)
{
    if (win_id < 0 || win_id >= num_windows) return;
    gui_ensure_pixels(win_id);
    gui_window_t* win = &windows[win_id];
    if (!win->pixels || !str) return;
    int cx = x;
    int start_x = x;
    int max_y = y;
    for (int i = 0; str[i]; i++)
    {
        if (str[i] == '\n') { cx = x; y += FONT_HEIGHT; max_y = y; continue; }
        int ci = (unsigned char)str[i] - FONT_FIRST_CHAR;
        if (ci < 0 || ci >= FONT_NUM_CHARS) continue;
        for (int row = 0; row < FONT_HEIGHT; row++)
        {
            int py = y + row;
            if (py < 0 || py >= win->ph) continue;
            unsigned char bits = font_data[ci][row];
            for (int col = 0; col < FONT_WIDTH; col++)
            {
                int px = cx + col;
                if (px < 0 || px >= win->pw) continue;
                win->pixels[py * win->pw + px] = (bits & (1 << (7 - col))) ? fg : bg;
            }
        }
        cx += FONT_WIDTH;
    }
    mark_window_dirty(win_id, start_x, y - ((max_y > y) ? (max_y - y) : 0), cx - start_x, max_y - y + FONT_HEIGHT);
}

void gui_init(void)
{
    num_windows = 0;
    active_window = -1;
    initialized = 1;
    prev_buttons = 0;
    cmd_should_exit = 0;
    cascade_x = 20;
    cascade_y = 40;
    start_menu_open = 0;
    start_menu_hovered = -1;
}

int gui_create(const char* title, int w, int h)
{
    if (num_windows >= GUI_MAX_WINDOWS) return -1;
    int idx = num_windows++;
    gui_window_t* win = &windows[idx];
    int slen = strlen(title);
    if (slen >= (int)sizeof(win->title)) slen = sizeof(win->title) - 1;
    memcpy(win->title, title, slen);
    win->title[slen] = 0;
    win->x = cascade_x;
    win->y = cascade_y;
    win->w = w;
    win->h = h;
    win->visible = 1;
    win->minimized = 0;
    win->maximized = 0;
    win->pixels = 0;
    int gf = XP_BORDER_W;
    win->cw = (w - gf * 2 - XP_SCROLLBAR_W) / FONT_WIDTH;
    win->ch = (h - GUI_TITLE_HEIGHT - gf * 2 - 1) / FONT_HEIGHT;
    win->cursor_x = 0;
    win->cursor_y = 0;
    win->num_buttons = 0;
    win->dragging = 0;
    win->resizing = 0;
    win->event_head = 0;
    win->event_tail = 0;
    win->btn_close_hover = 0;
    win->btn_max_hover = 0;
    win->btn_min_hover = 0;
    win->scroll_offset = 0;
    win->scroll_max = 0;
    if (win->cw < 1) win->cw = 1;
    if (win->ch < 1) win->ch = 1;
    win->pw = win->w - gf * 2;
    win->ph = win->h - GUI_TITLE_HEIGHT - gf * 2 - 1;
    if (win->pw < 1) win->pw = 1;
    if (win->ph < 1) win->ph = 1;
    gui_pixel_alloc(win);
    win->content = malloc(win->cw * win->ch);
    if (win->content)
        memset(win->content, ' ', win->cw * win->ch);
    active_window = idx;
    cascade_x += 20;
    cascade_y += 20;
    if ((uint32_t)(cascade_x + w) > fb_info.width - 40) cascade_x = 20;
    if ((uint32_t)(cascade_y + h) > fb_info.height - XP_TASKBAR_H - 40)
        cascade_y = 40;
    mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
    return idx;
}

static void mark_terminal_dirty(int idx)
{
    if (idx < 0 || idx >= num_windows) return;
    gui_window_t* w = &windows[idx];
    mark_window_dirty(idx, 0, 0, w->pw, w->ph);
}

void gui_putchar(int idx, char c)
{
    if (idx < 0 || idx >= num_windows) return;
    gui_window_t* w = &windows[idx];
    if (!w->content) return;
    if (c == '\n') { w->cursor_x = 0; w->cursor_y++; }
    else if (c == '\r') { w->cursor_x = 0; }
    else if (c == '\b')
    {
        if (w->cursor_x > 0)
        {
            w->cursor_x--;
            w->content[w->cursor_y * w->cw + w->cursor_x] = ' ';
        }
    }
    else if (c >= ' ')
    {
        w->content[w->cursor_y * w->cw + w->cursor_x] = c;
        w->cursor_x++;
        if (w->cursor_x >= w->cw) { w->cursor_x = 0; w->cursor_y++; }
    }
    if (w->cursor_y >= w->ch)
    {
        for (int r = 0; r < w->ch - 1; r++)
            memcpy(w->content + r * w->cw, w->content + (r + 1) * w->cw, w->cw);
        memset(w->content + (w->ch - 1) * w->cw, ' ', w->cw);
        w->cursor_y = w->ch - 1;
    }
    int visible_rows = (w->h - GUI_TITLE_HEIGHT - XP_BORDER_W * 2 - 1) / FONT_HEIGHT;
    if (w->scroll_offset + visible_rows <= w->cursor_y)
        w->scroll_offset = w->cursor_y - visible_rows + 1;
    if (w->scroll_offset < 0) w->scroll_offset = 0;
    mark_terminal_dirty(idx);
}

void gui_puts(int idx, const char* str)
{
    for (int i = 0; str[i]; i++)
        gui_putchar(idx, str[i]);
}

void gui_clear(int idx)
{
    if (idx < 0 || idx >= num_windows) return;
    gui_window_t* w = &windows[idx];
    if (w->pixels)
        memset(w->pixels, 0xFF, (uint32_t)(w->pw * w->ph * 4));
    if (w->content)
    {
        memset(w->content, ' ', w->cw * w->ch);
        w->cursor_x = 0;
        w->cursor_y = 0;
    }
    if (w->pixels)
        mark_window_dirty(idx, 0, 0, w->pw, w->ph);
    else
        mark_terminal_dirty(idx);
}

int gui_add_button(int idx, const char* label, int bx, int by, int bw, void (*cb)(int, int))
{
    if (idx < 0 || idx >= num_windows) return -1;
    gui_window_t* w = &windows[idx];
    if (w->num_buttons >= GUI_MAX_BUTTONS) return -1;
    int bi = w->num_buttons++;
    gui_button_t* btn = &w->buttons[bi];
    btn->x = bx;
    btn->y = by;
    btn->w = bw;
    int slen = strlen(label);
    if (slen >= (int)sizeof(btn->label)) slen = sizeof(btn->label) - 1;
    memcpy(btn->label, label, slen);
    btn->label[slen] = 0;
    btn->on_click = cb;
    return bi;
}

void gui_render(void)
{
    if (!initialized) return;

    uint8_t buttons = mouse_get_buttons_wrapper();
    int mx = mouse_get_x_wrapper();
    int my = mouse_get_y_wrapper();
    static int prev_mx = 0, prev_my = 0;

    int click_detected = (buttons & 1) && !(prev_buttons & 1);

    if (buttons != prev_buttons)
    {
        mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
        prev_buttons = buttons;
    }

    if (mx != prev_mx || my != prev_my)
    {
        mark_screen_dirty(prev_mx - 5, prev_my - 5, 20, 20);
        mark_screen_dirty(mx - 5, my - 5, 20, 20);
        prev_mx = mx;
        prev_my = my;
    }

    if (!screen_dirty)
        return;

    fb_backbuffer_alloc();

    draw_desktop();
    draw_desktop_icons();

    /* Start menu hover logic */
    if (start_menu_open)
    {
        int tby = fb_info.height - XP_TASKBAR_H;
        int mmx = mouse_get_x_wrapper();
        int mmy = mouse_get_y_wrapper();
        int prev_hovered = start_menu_hovered;
        start_menu_hovered = -1;
        int max_items = start_left_count > start_right_count ? start_left_count : start_right_count;
        int total_h = XP_SM_HEADER_H + max_items * XP_SM_ITEM_H + XP_SM_BOTTOM_H;
        int smy = tby - total_h;
        if (mmx >= 0 && mmx < XP_SM_TOTAL_W && mmy >= smy && mmy < smy + total_h)
        {
            if (mmy < smy + XP_SM_HEADER_H) goto sm_hover_done;
            int sep_x = XP_SM_LEFT_W;
            int item_area_y = smy + XP_SM_HEADER_H;
            int bottom_y = smy + XP_SM_HEADER_H + max_items * XP_SM_ITEM_H;
            int idx = (mmy - item_area_y) / XP_SM_ITEM_H;
            if (mmy >= bottom_y)
            {
                int shutdown_x = XP_SM_TOTAL_W - 100;
                int logoff_x = shutdown_x - 88;
                if (mmx >= shutdown_x && mmx < shutdown_x + 94)
                    start_menu_hovered = start_left_count + start_right_count;
                else if (mmx >= logoff_x && mmx < logoff_x + 84)
                    start_menu_hovered = start_left_count + start_right_count + 1;
            }
            else
            {
                if (mmx < sep_x)
                {
                    if (idx >= 0 && idx < start_left_count)
                        start_menu_hovered = idx;
                }
                else
                {
                    if (idx >= 0 && idx < start_right_count)
                        start_menu_hovered = start_left_count + idx;
                }
            }
        }
        if (start_menu_hovered != prev_hovered)
            mark_screen_dirty(0, smy, XP_SM_TOTAL_W, total_h);
    sm_hover_done: ;
    }

    /* Track dragging and resizing */
    for (int i = 0; i < num_windows; i++)
    {
        gui_window_t* w = &windows[i];
        if (w->dragging)
        {
            int new_x = mx - w->drag_off_x;
            int new_y = my - w->drag_off_y;
            if (new_x != w->x || new_y != w->y)
            {
                mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
                w->x = new_x;
                w->y = new_y;
                w->drag_outline_x = new_x;
                w->drag_outline_y = new_y;
            }
        }
        if (w->resizing)
        {
            int dx = mx - w->drag_off_x;
            int dy = my - w->drag_off_y;
            int min_w = 100;
            int min_h = 60;
            int new_x = w->x, new_y = w->y, new_w = w->w, new_h = w->h;
            if (w->resize_edge & 1) { new_x = w->x + dx; new_w = w->w - dx; }
            if (w->resize_edge & 2) { new_w = w->w + dx; }
            if (w->resize_edge & 4) { new_y = w->y + dy; new_h = w->h - dy; }
            if (w->resize_edge & 8) { new_h = w->h + dy; }
            if (new_w < min_w) new_w = min_w;
            if (new_h < min_h) new_h = min_h;
            if ((w->resize_edge & 1) && w->x + w->w - new_x < min_w) new_x = w->x + w->w - min_w;
            if ((w->resize_edge & 4) && w->y + w->h - new_y < min_h) new_y = w->y + w->h - min_h;
            if (new_x != w->x || new_y != w->y || new_w != w->w || new_h != w->h)
            {
                mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
                w->x = new_x; w->y = new_y; w->w = new_w; w->h = new_h;
                if (w->pixels)
                {
                    if (!w->pixels_page_allocated)
                        free(w->pixels);
                    w->pixels = NULL;
                }
                w->pw = w->w - XP_BORDER_W * 2; if (w->pw < 1) w->pw = 1;
                w->ph = w->h - GUI_TITLE_HEIGHT - XP_BORDER_W * 2 - 1; if (w->ph < 1) w->ph = 1;
                gui_pixel_alloc(w);
                w->drag_off_x = mx;
                w->drag_off_y = my;
            }
        }
    }

    /* Draw windows (non-active first for z-order) */
    for (int i = 0; i < num_windows; i++)
    {
        if (i != active_window && windows[i].visible && !windows[i].minimized)
        {
            draw_window_shadow(&windows[i]);
            draw_window(i);
        }
    }
    if (active_window >= 0 && windows[active_window].visible && !windows[active_window].minimized)
    {
        draw_window_shadow(&windows[active_window]);
        draw_window(active_window);
    }

    draw_taskbar();
    draw_start_menu();

    /* Finish drag and resize */
    for (int i = 0; i < num_windows; i++)
    {
        if (windows[i].dragging && !(buttons & 1))
        {
            mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
            windows[i].dragging = 0;
        }
        if (windows[i].resizing && !(buttons & 1))
        {
            mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
            windows[i].resizing = 0;
        }
    }

    if (click_detected)
        handle_click();

    draw_mouse_cursor();

    screen_dirty = 0;
    screen_dirty_w = 0;
    screen_dirty_h = 0;
    for (int i = 0; i < num_windows; i++)
    {
        windows[i].dirty = 0;
        windows[i].dirty_w = 0;
        windows[i].dirty_h = 0;
    }

    fb_blit();
}

void gui_set_active(int idx)
{
    if (idx >= 0 && idx < num_windows)
    {
        active_window = idx;
        mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
    }
}

void gui_set_content_click_callback(int win_id, void (*cb)(int, int, int))
{
    if (win_id < 0 || win_id >= num_windows) return;
    windows[win_id].on_content_click = cb;
}

void gui_set_title(int win_id, const char* title)
{
    if (win_id < 0 || win_id >= num_windows) return;
    int slen = strlen(title);
    if (slen >= (int)sizeof(windows[win_id].title)) slen = sizeof(windows[win_id].title) - 1;
    memcpy(windows[win_id].title, title, slen);
    windows[win_id].title[slen] = 0;
    gui_window_t* w = &windows[win_id];
    mark_screen_dirty(w->x, w->y, w->w, GUI_TITLE_HEIGHT + 1);
}

void gui_get_window_rect(int win_id, int* x, int* y, int* w, int* h)
{
    if (win_id < 0 || win_id >= num_windows) return;
    if (x) *x = windows[win_id].x;
    if (y) *y = windows[win_id].y;
    if (w) *w = windows[win_id].w;
    if (h) *h = windows[win_id].h;
}

int gui_get_event(int win_id, gui_event_t* ev)
{
    if (win_id < 0 || win_id >= num_windows) return 0;
    gui_window_t* w = &windows[win_id];
    if (w->event_head == w->event_tail) return 0;
    if (ev)
    {
        ev->type = w->event_queue[w->event_head].type;
        ev->mx = w->event_queue[w->event_head].mx;
        ev->my = w->event_queue[w->event_head].my;
        ev->buttons = w->event_queue[w->event_head].buttons;
    }
    int type = w->event_queue[w->event_head].type;
    w->event_head++;
    if (w->event_head == GUI_EVENT_QUEUE_SIZE) w->event_head = 0;
    return type;
}

void gui_minimize_window(int win_id)
{
    if (win_id < 0 || win_id >= num_windows) return;
    windows[win_id].minimized = 1;
    mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
}

void gui_maximize_window(int win_id)
{
    if (win_id < 0 || win_id >= num_windows) return;
    gui_window_t* w = &windows[win_id];
    if (!w->maximized)
    {
        w->restore_x = w->x;
        w->restore_y = w->y;
        w->restore_w = w->w;
        w->restore_h = w->h;
        w->x = 0;
        w->y = 0;
        w->w = fb_info.width;
        w->h = fb_info.height - XP_TASKBAR_H - GUI_TITLE_HEIGHT - 2;
        w->maximized = 1;
        mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
    }
}

void gui_restore_window(int win_id)
{
    if (win_id < 0 || win_id >= num_windows) return;
    gui_window_t* w = &windows[win_id];
    if (w->maximized)
    {
        w->x = w->restore_x;
        w->y = w->restore_y;
        w->w = w->restore_w;
        w->h = w->restore_h;
        w->maximized = 0;
        mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
    }
}

void gui_close_window(int win_id)
{
    if (win_id < 0 || win_id >= num_windows) return;
    windows[win_id].visible = 0;
    windows[win_id].minimized = 0;
    if (active_window == win_id)
        active_window = -1;
    mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
}

int gui_is_window_visible(int win_id)
{
    if (win_id < 0 || win_id >= num_windows) return 0;
    return windows[win_id].visible;
}

void gui_set_window_pos(int win_id, int x, int y)
{
    if (win_id < 0 || win_id >= num_windows) return;
    windows[win_id].x = x;
    windows[win_id].y = y;
    mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
}

void gui_set_window_size(int win_id, int w, int h)
{
    if (win_id < 0 || win_id >= num_windows) return;
    if (w < 100) w = 100;
    if (h < 60) h = 60;
    windows[win_id].w = w;
    windows[win_id].h = h;
    windows[win_id].cw = (w - XP_BORDER_W * 2 - XP_SCROLLBAR_W) / FONT_WIDTH;
    windows[win_id].ch = (h - GUI_TITLE_HEIGHT - XP_BORDER_W * 2 - 1) / FONT_HEIGHT;
    if (windows[win_id].cw < 1) windows[win_id].cw = 1;
    if (windows[win_id].ch < 1) windows[win_id].ch = 1;
    gui_ensure_pixels(win_id);
    mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
}

void gui_set_window_minimized(int win_id, int minimized)
{
    if (win_id < 0 || win_id >= num_windows) return;
    windows[win_id].minimized = minimized ? 1 : 0;
    mark_screen_dirty(0, 0, fb_info.width, fb_info.height);
}
