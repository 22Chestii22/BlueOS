#include "types.h"
#include "string.h"
#include "mem.h"
#include "screen.h"
#include "fb.h"
#include "gui.h"
#include "font.h"
#include "pe.h"
#include "serial.h"

#include "module.h"

static gui_window_t windows[GUI_MAX_WINDOWS];
static int num_windows = 0;
static int active_window = -1;
static uint8_t prev_buttons = 0;
static int initialized = 0;

static gui_menu_t menus[GUI_MAX_MENUS];
static int num_menus = 0;
volatile int cmd_should_exit = 0;

static int gui_terminal_win = -1;

static int cascade_x = 20;
static int cascade_y = 40;

static int start_menu_open = 0;
static int start_menu_hovered = -1;
static int start_button_down = 0;

static const char* start_left_items[] = {
    "Scout",
    "CMD",
    "RENDER",
    NULL
};
static const char* start_left_paths[] = {
    "\\SYSTEM\\PROGRAMS\\SCOUT.EXE",
    "\\SYSTEM\\PROGRAMS\\CMD.EXE",
    "\\SYSTEM\\PROGRAMS\\RENDER.EXE",
};
static int start_left_count = 3;

static const char* start_right_items[] = {
    "Documents",
    "Computer",
    "Control Panel",
    "Help & Support",
    "Run...",
    NULL
};
static int start_right_count = 5;

#define XP_SM_LEFT_W     170
#define XP_SM_RIGHT_W    140
#define XP_SM_TOTAL_W    (XP_SM_LEFT_W + XP_SM_RIGHT_W)
#define XP_SM_HEADER_H   50
#define XP_SM_BOTTOM_H   30
#define XP_SM_ITEM_H     (FONT_HEIGHT + 6)

typedef struct {
    const char* label;
    const char* path;
    int x, y, w, h;
} desktop_icon_t;

static desktop_icon_t desktop_icons[] = {
    {"CMD",   "\\SYSTEM\\PROGRAMS\\CMD.EXE",   20, 60, 64, 72},
};
static int num_desktop_icons = 1;

int gui_create_terminal(const char* title, int w, int h)
{
    gui_terminal_win = gui_create(title, w, h);
    return gui_terminal_win;
}

int gui_get_terminal(void)
{
    return gui_terminal_win;
}

void gui_clear_terminal(void)
{
    if (gui_terminal_win >= 0)
        gui_clear(gui_terminal_win);
}

static void draw_3d_rect(int x, int y, int w, int h, int raised)
{
    if (raised)
    {
        fb_draw_hline(y, x, x + w - 1, COL_WHITE);
        fb_draw_vline(x, y, y + h - 1, COL_WHITE);
        fb_draw_hline(y + h - 1, x, x + w - 1, COL_XP_BTN_SHADOW);
        fb_draw_vline(x + w - 1, y, y + h - 1, COL_XP_BTN_SHADOW);
    }
    else
    {
        fb_draw_hline(y, x, x + w - 1, COL_XP_BTN_SHADOW);
        fb_draw_vline(x, y, y + h - 1, COL_XP_BTN_SHADOW);
        fb_draw_hline(y + h - 1, x, x + w - 1, COL_WHITE);
        fb_draw_vline(x + w - 1, y, y + h - 1, COL_WHITE);
    }
}

static void draw_win3d_rect(int x, int y, int w, int h, int raised)
{
    if (raised)
    {
        fb_draw_hline(y, x, x + w - 1, COL_WHITE);
        fb_draw_vline(x, y, y + h - 1, COL_WHITE);
        fb_draw_hline(y + 1, x + 1, x + w - 2, COL_XP_BTN_FACE);
        fb_draw_vline(x + 1, y + 1, y + h - 2, COL_XP_BTN_FACE);
        fb_draw_hline(y + h - 1, x, x + w - 1, COL_XP_BTN_SHADOW);
        fb_draw_vline(x + w - 1, y, y + h - 1, COL_XP_BTN_SHADOW);
        fb_draw_hline(y + h - 2, x + 1, x + w - 2, COL_XP_BTN_BORDER);
        fb_draw_vline(x + w - 2, y + 1, y + h - 2, COL_XP_BTN_BORDER);
    }
    else
    {
        fb_draw_hline(y, x, x + w - 1, COL_XP_BTN_SHADOW);
        fb_draw_vline(x, y, y + h - 1, COL_XP_BTN_SHADOW);
        fb_draw_hline(y + 1, x + 1, x + w - 2, COL_XP_BTN_BORDER);
        fb_draw_vline(x + 1, y + 1, y + h - 2, COL_XP_BTN_BORDER);
        fb_draw_hline(y + h - 1, x, x + w - 1, COL_WHITE);
        fb_draw_vline(x + w - 1, y, y + h - 1, COL_WHITE);
        fb_draw_hline(y + h - 2, x + 1, x + w - 2, COL_XP_BTN_FACE);
        fb_draw_vline(x + w - 2, y + 1, y + h - 2, COL_XP_BTN_FACE);
    }
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

static void draw_menu_dropdown(gui_menu_t* menu)
{
    int dx = menu->x;
    int dy = GUI_MENU_HEIGHT;
    int item_h = FONT_HEIGHT + 4;
    int dh = menu->num_items * item_h + 2;

    fb_fillrect(dx, dy, GUI_MENU_DROPDOWN_W, dh, COL_XP_MENU_BG);
    draw_win3d_rect(dx, dy, GUI_MENU_DROPDOWN_W, dh, 1);

    for (int i = 0; i < menu->num_items; i++)
    {
        int iy = dy + 1 + i * item_h;
        uint32_t bg = COL_XP_MENU_BG;
        uint32_t fg = menu->items[i].enabled ? COL_BLACK : COL_LIGHT_GRAY;

        if (menu->hovered == i && menu->items[i].enabled)
        {
            bg = COL_XP_MENU_HIGHLIGHT;
            fg = COL_WHITE;
        }

        fb_fillrect(dx + 1, iy, GUI_MENU_DROPDOWN_W - 2, item_h, bg);
        fb_drawstring(dx + 3, iy + 2, menu->items[i].label, fg, bg);
    }
}

static void draw_menu_bar(void)
{
    fb_fillrect(0, 0, fb_info.width, GUI_MENU_HEIGHT, COL_XP_BTN_FACE);
    draw_win3d_rect(0, 0, fb_info.width, GUI_MENU_HEIGHT, 0);

    int x = 4;
    for (int i = 0; i < num_menus; i++)
    {
        menus[i].x = x;
        int label_w = strlen(menus[i].label) * FONT_WIDTH;

        uint32_t bg = menus[i].is_open ? COL_XP_MENU_HIGHLIGHT : COL_XP_BTN_FACE;
        uint32_t fg = menus[i].is_open ? COL_WHITE : COL_BLACK;

        fb_fillrect(x - 2, 0, label_w + 4, GUI_MENU_HEIGHT, bg);
        fb_drawstring(x, 2, menus[i].label, fg, bg);

        x += label_w + 8;

        if (menus[i].is_open)
            draw_menu_dropdown(&menus[i]);
    }

    const char* title = "BlueOS";
    int tx = fb_info.width - strlen(title) * FONT_WIDTH - 6;
    fb_drawstring(tx, 2, title, COL_DARK_GRAY, COL_XP_BTN_FACE);
}

static void draw_desktop_icons(void)
{
    for (int i = 0; i < num_desktop_icons; i++)
    {
        desktop_icon_t* di = &desktop_icons[i];
        /* Draw desktop icon background with slight glass effect */
        fb_fillrect_alpha(di->x, di->y, di->w, di->h, FB_RGB(0x40, 0x80, 0xC0), 60);
        /* Icon area — small colored square */
        fb_fillrect(di->x + 8, di->y + 4, 48, 40, FB_RGB(0x20, 0x60, 0xA0));
        /* Draw label with shadow for readability */
        fb_drawstring(di->x + 4, di->y + 48, di->label, FB_RGB(0x00, 0x00, 0x00), 0);
        fb_drawstring(di->x + 3, di->y + 47, di->label, COL_WHITE, 0);
    }
}

static void draw_start_menu(void)
{
    if (!start_menu_open) return;

    int tby = fb_info.height - GUI_TASK_HEIGHT;
    int mx = 2;
    int max_items = start_left_count > start_right_count ? start_left_count : start_right_count;
    int my = tby - XP_SM_HEADER_H - max_items * XP_SM_ITEM_H - XP_SM_BOTTOM_H;

    int total_h = XP_SM_HEADER_H + max_items * XP_SM_ITEM_H + XP_SM_BOTTOM_H;

    fb_fillrect(mx, my, XP_SM_TOTAL_W, total_h, COL_W7_SM_LEFT_BG);

    /* Header — Win7-style: light gray/white with subtle gradient and user picture */
    int hx = mx + 2, hy = my + 2;
    int hw = XP_SM_TOTAL_W - 4, hh = XP_SM_HEADER_H - 2;
    for (int row = 0; row < hh; row++)
    {
        uint8_t v = 0xF0 + (uint32_t)(0xFA - 0xF0) * row / hh;
        fb_draw_hline(hy + row, hx, hx + hw - 1, FB_RGB(v, v, v));
    }
    /* User icon — Win7 circular user picture */
    int icon_x = hx + 8;
    int icon_y = hy + (hh - 32) / 2;
    fb_fillrect(icon_x, icon_y, 32, 32, FB_RGB(0xD0, 0xD0, 0xD0));
    for (int row = 0; row < 32; row++)
    {
        for (int col = 0; col < 32; col++)
        {
            int dx = col - 16, dy = row - 16;
            if (dx * dx + dy * dy <= 14 * 14)
            {
                int c = 0xD0 - (dx*dx + dy*dy) / 20;
                if (c < 0xA0) c = 0xA0;
                fb_putpixel(icon_x + col, icon_y + row, FB_RGB(c, c, c));
            }
        }
    }
    fb_drawstring(icon_x + 11, icon_y + 10, "U", FB_RGB(0x80, 0x80, 0x80), 0);
    /* User name */
    fb_drawstring(icon_x + 40, hy + 8, "Default User", COL_BLACK, FB_RGB(0xF5, 0xF5, 0xF5));

    /* Bottom border of header */
    fb_draw_hline(hy + hh, hx, hx + hw - 1, COL_W7_SM_BORDER);

    /* Left column — Win7 pinned programs list */
    int left_x = mx + 2;
    int left_y = my + XP_SM_HEADER_H;
    int left_w = XP_SM_LEFT_W;

    for (int i = 0; i < start_left_count; i++)
    {
        int iy = left_y + i * XP_SM_ITEM_H;
        uint32_t bg = COL_W7_SM_LEFT_BG;
        uint32_t fg = COL_BLACK;

        if (start_menu_hovered >= 0 && start_menu_hovered < start_left_count &&
            start_menu_hovered == i)
        {
            bg = COL_W7_SM_HIGHLIGHT;
            fg = COL_BLACK;
        }

        fb_fillrect(left_x, iy, left_w, XP_SM_ITEM_H, bg);

        /* Win7-style program icon — colored square with letter */
        fb_fillrect(left_x + 4, iy + 3, 14, 14, FB_RGB(0x30, 0x80, 0xD0));
        char icon_char = start_left_items[i][0];
        fb_drawstring(left_x + 7, iy + 2, &icon_char, COL_WHITE, FB_RGB(0x30, 0x80, 0xD0));
        fb_drawstring(left_x + 24, iy + 2, start_left_items[i], fg, bg);
    }

    /* Etched separators in left column between programs */
    if (start_left_count > 0)
    {
        fb_draw_hline(left_y + start_left_count * XP_SM_ITEM_H - 1, left_x + 6, left_x + left_w - 6, COL_XP_SM_SEPARATOR);
    }

    /* "All Programs" link after separator */
    int all_prog_y = left_y + start_left_count * XP_SM_ITEM_H;
    {
        uint32_t bg = COL_XP_SM_LEFT_BG;
        uint32_t fg = FB_RGB(0x00, 0x60, 0xCC);
        int all_idx = start_left_count;
        if (start_menu_hovered == all_idx)
        {
            bg = COL_XP_HIGHLIGHT;
            fg = COL_WHITE;
        }
        fb_fillrect(left_x, all_prog_y, left_w, XP_SM_ITEM_H, bg);
        fb_drawstring(left_x + 24, all_prog_y + 2, "All Programs", fg, bg);
        fb_drawstring(left_x + left_w - FONT_WIDTH - 6, all_prog_y + 2, "\x10", fg, bg);
    }

    /* Separator between columns */
    int sep_x = left_x + left_w;
    fb_draw_vline(sep_x, left_y, left_y + max_items * XP_SM_ITEM_H - 1, COL_XP_SM_SEPARATOR);

    /* Right column — Win7-style system items */
    int right_x = sep_x + 2;
    int right_w = XP_SM_RIGHT_W - 4;

    for (int i = 0; i < start_right_count; i++)
    {
        int iy = left_y + i * XP_SM_ITEM_H;

        /* Section separator between "Computer" group and "Help/Run" group */
        if (i == 3)
        {
            fb_draw_hline(iy - 1, right_x + 4, right_x + right_w - 4, COL_XP_SM_SEPARATOR);
        }

        uint32_t bg = COL_XP_SM_RIGHT_BG;
        uint32_t fg = COL_BLACK;

        int adj_idx = start_left_count + 1 + i;
        if (start_menu_hovered == adj_idx)
        {
            bg = COL_XP_HIGHLIGHT;
            fg = COL_WHITE;
        }

        fb_fillrect(right_x, iy, right_w, XP_SM_ITEM_H, bg);
        fb_drawstring(right_x + 6, iy + 2, start_right_items[i], fg, bg);
    }

    /* Separator between items and search area */
    int right_bottom = left_y + (start_right_count > start_left_count ? start_right_count : start_left_count) * XP_SM_ITEM_H;
    int sep2_y = right_bottom;
    fb_draw_hline(sep2_y, mx + 2, mx + XP_SM_TOTAL_W - 3, COL_XP_SM_SEPARATOR);

    /* Search box area (Win7: "Search programs and files") */
    int search_y = sep2_y + 1;
    int search_h = W7_SM_SEARCH_H;
    fb_fillrect(mx + 4, search_y, XP_SM_TOTAL_W - 8, search_h - 2, COL_W7_SM_SEARCH_BG);
    draw_3d_rect(mx + 4, search_y, XP_SM_TOTAL_W - 8, search_h - 2, 0);
    fb_drawstring(mx + 10, search_y + 4, "Search programs and files", FB_RGB(0x80, 0x80, 0x80), COL_W7_SM_SEARCH_BG);

    /* Bottom bar — Shut down button (Win7 style) */
    int bottom_y = search_y + search_h - 1;
    int bottom_h = W7_SM_BOTTOM_H;
    fb_fillrect(mx + 2, bottom_y, XP_SM_TOTAL_W - 4, bottom_h, FB_RGB(0xE0, 0xE0, 0xE0));

    int shutdown_idx = start_left_count + 1 + start_right_count;
    int shutdown_x = mx + XP_SM_TOTAL_W - 100;
    int shutdown_w = 96;
    int shutdown_btn_y = bottom_y + 4;

    uint32_t s_bg = FB_RGB(0xE0, 0xE0, 0xE0);
    uint32_t s_fg = COL_BLACK;
    if (start_menu_hovered == shutdown_idx)
    {
        s_bg = COL_XP_HIGHLIGHT;
        s_fg = COL_WHITE;
    }
    fb_fillrect(shutdown_x, shutdown_btn_y, shutdown_w, bottom_h - 8, s_bg);
    fb_drawstring(shutdown_x + 6, shutdown_btn_y + 2, "Shut Down", s_fg, s_bg);
    fb_fillrect(shutdown_x + 62, shutdown_btn_y + 2, 14, 14, FB_RGB(0x80, 0x30, 0x30));
    fb_drawstring(shutdown_x + 66, shutdown_btn_y + 2, "\x10", COL_WHITE, FB_RGB(0x80, 0x30, 0x30));
}

static void draw_window_title_bar(gui_window_t* w, int active)
{
    int x = w->x, y = w->y, tw = w->w;
    uint32_t fg, border_col;

    uint32_t glow_col;

    if (active)
    {
        glow_col = COL_W7_AERO_GLOW_ACT;
        fg = COL_BLACK;
        border_col = FB_RGB(0x70, 0x70, 0x70);
    }
    else
    {
        glow_col = COL_W7_AERO_GLOW;
        fg = FB_RGB(0x60, 0x60, 0x60);
        border_col = FB_RGB(0xA0, 0xA0, 0xA0);
    }

    /* Aero glass background with gradient */
    for (int row = 0; row < GUI_TITLE_HEIGHT; row++)
    {
        uint8_t alpha = 180 + (uint32_t)(60 * row) / GUI_TITLE_HEIGHT;
        uint32_t col = active ? FB_RGB(0xE8, 0xF0, 0xF8) : FB_RGB(0xE0, 0xE0, 0xE0);
        fb_fillrect_alpha(x, y + row, tw, 1, col, alpha);
    }

    /* Glass reflection line at top */
    fb_draw_hline(y + 1, x + 2, x + tw - 3, FB_RGB(0xFF, 0xFF, 0xFF));
    /* Bottom border line */
    fb_draw_hline(y + GUI_TITLE_HEIGHT - 1, x, x + tw - 1, border_col);
    /* Left/right borders */
    if (active)
    {
        fb_draw_vline(x, y, y + GUI_TITLE_HEIGHT - 1, border_col);
        fb_draw_vline(x + tw - 1, y, y + GUI_TITLE_HEIGHT - 1, border_col);
    }

    /* Title text with glow */
    int tx = x + 6;
    if (active)
    {
        fb_draw_glow_text(tx, y + 4, w->title, fg, glow_col);
    }
    else
    {
        fb_drawstring(tx, y + 4, w->title, fg, 0);
    }

    int cap_y = y + (GUI_TITLE_HEIGHT - 16) / 2;

    /* Minimize button — Aero style */
    if (!w->minimized)
    {
        int min_x = x + tw - 52;
        fb_fillrect(min_x, cap_y, 16, 16, FB_RGB(0xE0, 0xE0, 0xE0));
        fb_draw_hline(cap_y + 12, min_x + 4, min_x + 11, COL_W7_AERO_MIN);
    }

    /* Close button — Aero red */
    int close_x = x + tw - 34;
    fb_fillrect(close_x, cap_y, 16, 16, COL_W7_AERO_CLOSE);
    fb_drawstring(close_x + 4, cap_y + 1, "X", COL_WHITE, COL_W7_AERO_CLOSE);

    /* Maximize button — optional */
    int max_x = x + tw - 70;
    fb_fillrect(max_x, cap_y, 16, 16, FB_RGB(0xE0, 0xE0, 0xE0));
    draw_3d_rect(max_x, cap_y, 16, 16, 1);
}

static void draw_window_content(gui_window_t* w)
{
    int cx = w->x + 1;
    int cy = w->y + GUI_TITLE_HEIGHT + 1;
    int cw = w->w - 2;
    int ch = w->h - GUI_TITLE_HEIGHT - 3;

    fb_fillrect(cx, cy, cw, ch, COL_WHITE);

    if (w->pixels && w->pw > 0 && w->ph > 0)
    {
        for (int row = 0; row < w->ph && row < ch; row++)
        {
            for (int col = 0; col < w->pw && col < cw; col++)
            {
                uint32_t color = w->pixels[row * w->pw + col];
                if (color != 0xFFFFFFFF)
                    fb_putpixel((uint32_t)(cx + col), (uint32_t)(cy + row), color);
            }
        }
    }
    else
    {
        printf("[DWC] NO PIXELS or zero size\n");
    }

    if (w->content)
    {
        for (int row = 0; row < w->ch && row < ch / FONT_HEIGHT; row++)
        {
            for (int col = 0; col < w->cw && col < cw / FONT_WIDTH; col++)
            {
                char c = w->content[row * w->cw + col];
                if (c)
                    fb_drawchar(cx + col * FONT_WIDTH, cy + row * FONT_HEIGHT, c, COL_BLACK, COL_WHITE);
            }
        }
    }

    if (active_window >= 0 && &windows[active_window] == w)
    {
        int cur_x = cx + w->cursor_x * FONT_WIDTH;
        int cur_y = cy + w->cursor_y * FONT_HEIGHT;
        if (cur_x >= cx && cur_x < cx + cw && cur_y >= cy && cur_y < cy + ch)
        {
            for (int row = 0; row < FONT_HEIGHT; row++)
            {
                int py = cur_y + row;
                if (py < 0 || (uint32_t)py >= fb_info.height) continue;
                for (int col = 0; col < FONT_WIDTH; col++)
                {
                    int px = cur_x + col;
                    if (px < 0 || (uint32_t)px >= fb_info.width) continue;
                    uint32_t c = fb_getpixel(px, py);
                    fb_putpixel(px, py, ~c & 0x00FFFFFF);
                }
            }
        }
    }
}

static void draw_window_outline(gui_window_t* w)
{
    int x = w->drag_outline_x;
    int y = w->drag_outline_y;
    int ww = w->w;
    int wh = w->h;

    for (int i = 0; i < wh; i++)
    {
        for (int j = 0; j < ww; j++)
        {
            if (i == 0 || i == wh - 1 || j == 0 || j == ww - 1)
            {
                if ((i + j) & 1)
                {
                    int px = x + j;
                    int py = y + i;
                    if (px >= 0 && (uint32_t)px < fb_info.width && py >= 0 && (uint32_t)py < fb_info.height)
                        fb_putpixel(px, py, COL_BLACK);
                }
            }
        }
    }
}

static void draw_window(int idx)
{
    gui_window_t* w = &windows[idx];
    if (!w->visible) return;

    if (w->minimized) return;

    if (w->dragging)
    {
        draw_window_outline(w);
        return;
    }

    int active = (idx == active_window);

    fb_fillrect(w->x + 1, w->y + GUI_TITLE_HEIGHT + 1, w->w - 2, w->h - GUI_TITLE_HEIGHT - 2, FB_RGB(0xF0, 0xF0, 0xF0));

    draw_window_title_bar(w, active);

    uint32_t border = active ? COL_XP_WINDOW_BORDER_ACTIVE : COL_XP_WINDOW_BORDER_INACT;
    fb_draw_hline(w->y, w->x, w->x + w->w - 1, border);
    fb_draw_vline(w->x, w->y, w->y + w->h - 1, border);
    fb_draw_hline(w->y + w->h - 1, w->x, w->x + w->w - 1, border);
    fb_draw_vline(w->x + w->w - 1, w->y, w->y + w->h - 1, border);

    draw_window_content(w);

    for (int b = 0; b < w->num_buttons; b++)
    {
        gui_button_t* btn = &w->buttons[b];
        int bx = w->x + 1 + btn->x * FONT_WIDTH;
        int by = w->y + GUI_TITLE_HEIGHT + 1 + btn->y * FONT_HEIGHT;
        int bw = btn->w * FONT_WIDTH;
        int bh = FONT_HEIGHT + 4;

        fb_fillrect(bx, by, bw, bh, COL_XP_BTN_FACE);
        draw_win3d_rect(bx, by, bw, bh, 1);
        fb_drawstring(bx + 2, by + 2, btn->label, COL_BLACK, COL_XP_BTN_FACE);
    }
}

static void draw_mouse_cursor(void)
{
    if (!mouse_is_present_wrapper()) return;
    int mx = mouse_get_x_wrapper();
    int my = mouse_get_y_wrapper();
    if (mx < 0 || (uint32_t)mx >= fb_info.width || my < 0 || (uint32_t)my >= fb_info.height)
        return;

    static const unsigned char cursor_data[] = {
        0xFF, 0xC0,
        0xFF, 0xC0,
        0xFF, 0xE0,
        0xFF, 0xE0,
        0xFF, 0xF0,
        0xFF, 0xF0,
        0xFF, 0xF8,
        0xFF, 0xF8,
        0xFF, 0xFC,
        0x67, 0xFC,
        0x07, 0xFE,
        0x03, 0xFE,
        0x01, 0xFF,
        0x00, 0xFF,
        0x00, 0x7E,
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
                uint32_t fg = COL_WHITE;
                uint32_t max = FB_GET_R(bg);
                uint32_t g = FB_GET_G(bg);
                uint32_t b = FB_GET_B(bg);
                if (g > max) max = g;
                if (b > max) max = b;
                if (max > 128)
                    fg = COL_BLACK;
                fb_putpixel(px, py, fg);
            }
        }
    }
}

static void handle_start_menu_click(int mx, int my)
{
    if (!start_menu_open) return;

    int tby = fb_info.height - GUI_TASK_HEIGHT;

    int max_items = start_left_count > start_right_count ? start_left_count : start_right_count;
    int total_h = XP_SM_HEADER_H + max_items * XP_SM_ITEM_H + W7_SM_SEARCH_H + W7_SM_BOTTOM_H;
    int smx = 2;
    int smy = tby - total_h;

    if (mx < smx || mx >= smx + XP_SM_TOTAL_W ||
        my < smy || my >= smy + total_h)
    {
        start_menu_open = 0;
        return;
    }

    if (my < smy + XP_SM_HEADER_H)
        return;

    int item_area_y = smy + XP_SM_HEADER_H;
    int bottom_y = smy + XP_SM_HEADER_H + max_items * XP_SM_ITEM_H;
    int search_area_y = bottom_y;
    int shutdown_area_y = search_area_y + W7_SM_SEARCH_H;

    if (my >= shutdown_area_y)
    {
        /* Shut down button */
        int shutdown_x = smx + XP_SM_TOTAL_W - 100;
        if (mx >= shutdown_x && mx < shutdown_x + 96)
        {
            start_menu_open = 0;
            cmd_should_exit = 1;
        }
        return;
    }

    if (my >= search_area_y)
    {
        /* Search area — no action */
        return;
    }

    /* Item area - determine column by X */
    int sep_x = smx + XP_SM_LEFT_W;
    int idx = (my - item_area_y) / XP_SM_ITEM_H;
    if (idx < 0) return;

    if (mx < sep_x)
    {
        /* Left column: programs */
        if (idx < start_left_count)
        {
            start_menu_open = 0;
            if (start_left_paths[idx])
                pe_spawn(start_left_paths[idx]);
        }
        /* "All Programs" — no action for now */
    }
    else
    {
        /* Right column */
        if (idx >= start_right_count) return;
        start_menu_open = 0;
        if (strcmp(start_right_items[idx], "Run...") == 0)
        {
            if (gui_terminal_win >= 0)
                gui_puts(gui_terminal_win, "\nType a program name:\n");
        }
        else if (strcmp(start_right_items[idx], "Help & Support") == 0)
        {
            if (gui_terminal_win >= 0)
                gui_puts(gui_terminal_win, "\nBlueOS Help\n  Start menu for programs\n  Desktop icons\n  Window minimize/close\n\n");
        }
        else if (strcmp(start_right_items[idx], "Computer") == 0)
        {
            if (gui_terminal_win >= 0)
                gui_puts(gui_terminal_win, "\nComputer: BlueOS x86_64\n\n");
        }
        else if (strcmp(start_right_items[idx], "Documents") == 0)
        {
            if (gui_terminal_win >= 0)
                gui_puts(gui_terminal_win, "\nDocuments folder\n\n");
        }
        else if (strcmp(start_right_items[idx], "Control Panel") == 0)
        {
            if (gui_terminal_win >= 0)
                gui_puts(gui_terminal_win, "\nControl Panel\n\n");
        }
    }
}

static int handle_menu_click(int mx, int my)
{
    if (my >= 0 && my < GUI_MENU_HEIGHT)
    {
        for (int i = 0; i < num_menus; i++)
        {
            int end = menus[i].x + strlen(menus[i].label) * FONT_WIDTH + 4;
            if (mx >= menus[i].x - 2 && mx < end)
            {
                int was = menus[i].is_open;
                for (int j = 0; j < num_menus; j++)
                    menus[j].is_open = 0;
                menus[i].is_open = !was;
                return 1;
            }
        }
        for (int i = 0; i < num_menus; i++)
            menus[i].is_open = 0;
        return 1;
    }

    for (int i = 0; i < num_menus; i++)
    {
        if (!menus[i].is_open) continue;
        int item_h = FONT_HEIGHT + 4;
        int dh = menus[i].num_items * item_h + 2;
        if (mx >= menus[i].x && mx < menus[i].x + GUI_MENU_DROPDOWN_W &&
            my >= GUI_MENU_HEIGHT && my < GUI_MENU_HEIGHT + dh)
        {
            int idx = (my - GUI_MENU_HEIGHT - 1) / item_h;
            if (idx >= 0 && idx < menus[i].num_items && menus[i].items[idx].enabled)
            {
                if (strcmp(menus[i].items[idx].label, "Exit") == 0)
                    cmd_should_exit = 1;
                else if (strcmp(menus[i].items[idx].label, "About BlueOS") == 0)
                {
                    if (gui_terminal_win >= 0)
                        gui_puts(gui_terminal_win, "\nBlueOS x86_64 v1.0\n\n");
                }
                else if (strcmp(menus[i].items[idx].label, "Run...") == 0)
                {
                    if (gui_terminal_win >= 0)
                        gui_puts(gui_terminal_win, "\nRun...\n\n");
                }
            }
            for (int j = 0; j < num_menus; j++)
                menus[j].is_open = 0;
            return 1;
        }
    }

    for (int i = 0; i < num_menus; i++)
        menus[i].is_open = 0;
    return 0;
}

static int handle_taskbar_click(int mx, int my)
{
    int tby = fb_info.height - GUI_TASK_HEIGHT;
    if (my < tby || (uint32_t)my >= fb_info.height) return 0;

    /* Orb button area */
    int orb_x = 4;
    int orb_w = W7_ORB_SIZE + 4;
    if (mx >= orb_x && mx < orb_x + orb_w)
    {
        if (start_menu_open)
            start_menu_open = 0;
        else
            start_menu_open = 1;
        return 1;
    }

    int bx = orb_x + orb_w + 4;
    int sinfo_w = mouse_is_present_wrapper() ? 10 * FONT_WIDTH + 8 : 0;
    int max_w = fb_info.width - sinfo_w - bx - 4;

    for (int i = 0; i < num_windows; i++)
    {
        if (!windows[i].visible) continue;
        int bw = strlen(windows[i].title) * FONT_WIDTH + 14;
        if (bw > 180) bw = 180;
        if (bx + bw > max_w)
        {
            int remaining = max_w - bx;
            if (remaining < 30) break;
            bw = remaining;
        }
        if (mx >= bx && mx < bx + bw)
        {
            if (windows[i].minimized)
            {
                windows[i].minimized = 0;
                windows[i].visible = 1;
                bring_to_front(i);
            }
            else if (active_window != i)
            {
                bring_to_front(i);
            }
            else
            {
                windows[i].minimized = 1;
                windows[i].visible = 1;
            }
            return 1;
        }
        bx += bw + 2;
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
        int tby = fb_info.height - GUI_TASK_HEIGHT;
        int max_items = start_left_count > start_right_count ? start_left_count : start_right_count;
        int total_h = XP_SM_HEADER_H + max_items * XP_SM_ITEM_H + W7_SM_SEARCH_H + W7_SM_BOTTOM_H;
        int smx = 2;
        int smy = tby - total_h;

        int in_menu = (mx >= smx && mx < smx + XP_SM_TOTAL_W &&
                       my >= smy && my < smy + total_h);

        if (in_menu)
        {
            handle_start_menu_click(mx, my);
            return;
        }

        /* Clicking orb button closes start menu */
        int orb_x = 4;
        if (my >= tby && mx >= orb_x && mx < orb_x + W7_ORB_SIZE)
        {
            start_menu_open = 0;
            return;
        }

        start_menu_open = 0;
    }

    if (handle_menu_click(mx, my))
        return;

    if (handle_taskbar_click(mx, my))
        return;

    for (int i = 0; i < num_desktop_icons; i++)
    {
        desktop_icon_t* di = &desktop_icons[i];
        if (mx >= di->x && mx < di->x + di->w &&
            my >= di->y && my < di->y + di->h)
        {
            pe_spawn(di->path);
            return;
        }
    }

    for (int i = num_windows - 1; i >= 0; i--)
    {
        gui_window_t* w = &windows[i];
        if (!w->visible || w->minimized) continue;
        if (mx < w->x || mx >= w->x + w->w) continue;
        if (my < w->y || my >= w->y + w->h) continue;

        int cap_y = w->y + 2;
        int close_x = w->x + w->w - 17;
        int min_x = w->x + w->w - 33;

        if (my >= cap_y && my < cap_y + 14)
        {
            if (mx >= close_x + 1 && mx < close_x + 15)
            {
                w->visible = 0;
                w->minimized = 0;
                if (active_window == i)
                    active_window = -1;
                return;
            }
            if (mx >= min_x + 1 && mx < min_x + 15)
            {
                w->minimized = 1;
                if (active_window == i)
                    active_window = -1;
                return;
            }
        }

        if (i != active_window)
        {
            bring_to_front(i);
            w = &windows[active_window];
        }

        if (my >= w->y && my < w->y + GUI_TITLE_HEIGHT)
        {
            w->dragging = 1;
            w->drag_off_x = mx - w->x;
            w->drag_off_y = my - w->y;
            w->drag_outline_x = w->x;
            w->drag_outline_y = w->y;
            return;
        }

        int cx = mx - (w->x + 1);
        int cy = my - (w->y + GUI_TITLE_HEIGHT + 1);
        for (int b = 0; b < w->num_buttons; b++)
        {
            gui_button_t* btn = &w->buttons[b];
            int bx = btn->x * FONT_WIDTH;
            int by = btn->y * FONT_HEIGHT;
            if (cx >= bx && cx < bx + btn->w * FONT_WIDTH &&
                cy >= by && cy < by + FONT_HEIGHT + 4)
            {
                if (btn->on_click) btn->on_click(i, b);
                return;
            }
        }
        if (w->on_content_click)
        {
            w->on_content_click(i, mx, my);
            return;
        }
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
    if (w->pixels) return;
    w->pw = w->w - 2;
    w->ph = w->h - GUI_TITLE_HEIGHT - 3;
    if (w->pw < 1) w->pw = 1;
    if (w->ph < 1) w->ph = 1;
    uint32_t alloc_size = (uint32_t)(w->pw * w->ph * 4);
    w->pixels = malloc(alloc_size);
    if (w->pixels)
        memset(w->pixels, 0xFF, alloc_size);
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
}

void gui_draw_text(int win_id, int x, int y, const char* str, uint32_t fg, uint32_t bg)
{
    if (win_id < 0 || win_id >= num_windows) return;
    gui_ensure_pixels(win_id);
    gui_window_t* win = &windows[win_id];
    if (!win->pixels || !str) return;

    int cx = x;
    for (int i = 0; str[i]; i++)
    {
        if (str[i] == '\n')
        {
            cx = x;
            y += FONT_HEIGHT;
            continue;
        }
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
                uint32_t color = (bits & (1 << (7 - col))) ? fg : bg;
                win->pixels[py * win->pw + px] = color;
            }
        }
        cx += FONT_WIDTH;
    }
}

void gui_menu_init(void)
{
    num_menus = 0;

    gui_menu_t* file = &menus[num_menus++];
    strcpy(file->label, "File");
    file->is_open = 0;
    file->hovered = -1;
    strcpy(file->items[0].label, "Run...");
    file->items[0].enabled = 1;
    strcpy(file->items[1].label, "Exit");
    file->items[1].enabled = 1;
    file->num_items = 2;

    gui_menu_t* edit = &menus[num_menus++];
    strcpy(edit->label, "Edit");
    edit->is_open = 0;
    edit->hovered = -1;
    strcpy(edit->items[0].label, "Copy");
    edit->items[0].enabled = 1;
    strcpy(edit->items[1].label, "Paste");
    edit->items[1].enabled = 1;
    edit->num_items = 2;

    gui_menu_t* view = &menus[num_menus++];
    strcpy(view->label, "View");
    view->is_open = 0;
    view->hovered = -1;
    strcpy(view->items[0].label, "Refresh");
    view->items[0].enabled = 1;
    view->num_items = 1;

    gui_menu_t* help = &menus[num_menus++];
    strcpy(help->label, "Help");
    help->is_open = 0;
    help->hovered = -1;
    strcpy(help->items[0].label, "About BlueOS");
    help->items[0].enabled = 1;
    help->num_items = 1;
}

void gui_init(void)
{
    num_windows = 0;
    active_window = -1;
    initialized = 1;
    prev_buttons = 0;
    cmd_should_exit = 0;
    gui_menu_init();
    cascade_x = 20;
    cascade_y = 40;
    start_menu_open = 0;
    start_menu_hovered = -1;
    start_button_down = 0;
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
    win->pixels = 0;
    win->cw = (w - 2) / FONT_WIDTH;
    win->ch = (h - GUI_TITLE_HEIGHT - 3) / FONT_HEIGHT;
    win->cursor_x = 0;
    win->cursor_y = 0;
    win->num_buttons = 0;
    win->dragging = 0;
    win->event_head = 0;
    win->event_tail = 0;

    if (win->cw < 1) win->cw = 1;
    if (win->ch < 1) win->ch = 1;

    win->pw = win->w - 2;
    win->ph = win->h - GUI_TITLE_HEIGHT - 3;
    if (win->pw < 1) win->pw = 1;
    if (win->ph < 1) win->ph = 1;
    uint32_t pix_size = (uint32_t)(win->pw * win->ph * 4);
    win->pixels = malloc(pix_size);
    if (win->pixels)
        memset(win->pixels, 0xFF, pix_size);

    win->content = malloc(win->cw * win->ch);
    if (win->content)
        memset(win->content, ' ', win->cw * win->ch);

    active_window = idx;

    cascade_x += 20;
    cascade_y += 20;
    if ((uint32_t)(cascade_x + w) > fb_info.width - 40) cascade_x = 20;
    if ((uint32_t)(cascade_y + h) > fb_info.height - GUI_MENU_HEIGHT - GUI_TASK_HEIGHT - 40)
        cascade_y = 40;

    return idx;
}

void gui_putchar(int idx, char c)
{
    if (idx < 0 || idx >= num_windows) return;
    gui_window_t* w = &windows[idx];
    if (!w->content) return;

    if (c == '\n')
    {
        w->cursor_x = 0;
        w->cursor_y++;
    }
    else if (c == '\r')
    {
        w->cursor_x = 0;
    }
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
        if (w->cursor_x >= w->cw)
        {
            w->cursor_x = 0;
            w->cursor_y++;
        }
    }

    if (w->cursor_y >= w->ch)
    {
        for (int r = 0; r < w->ch - 1; r++)
            memcpy(w->content + r * w->cw, w->content + (r + 1) * w->cw, w->cw);
        memset(w->content + (w->ch - 1) * w->cw, ' ', w->cw);
        w->cursor_y = w->ch - 1;
    }
}

void gui_puts(int idx, const char* str)
{
    for (int i = 0; str[i]; i++)
        gui_putchar(idx, str[i]);
    serial_write("[GUI:");
    serial_dec(idx);
    serial_write(" '");
    serial_write(str);
    serial_write("']\n");
}

void gui_clear(int idx)
{
    if (idx < 0 || idx >= num_windows) return;
    gui_window_t* w = &windows[idx];
    if (w->pixels)
    {
        memset(w->pixels, 0xFF, (uint32_t)(w->pw * w->ph * 4));
    }
    if (w->content)
    {
        memset(w->content, ' ', w->cw * w->ch);
        w->cursor_x = 0;
        w->cursor_y = 0;
    }
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

static void draw_orb_button(int x, int y, int size, int hovered)
{
    int radius = size / 2;
    int cx = x + radius;
    int cy = y + radius;
    int r2 = radius * radius;
    for (int row = 0; row < size; row++)
    {
        for (int col = 0; col < size; col++)
        {
            int dx = col - radius;
            int dy = row - radius;
            int dist2 = dx * dx + dy * dy;
            if (dist2 <= r2)
            {
                uint8_t r, g, b;
                if (dist2 < (radius * radius / 4))
                {
                    /* Inner glow — brighter */
                    if (hovered)
                        r = 0xA0, g = 0xE8, b = 0x50;
                    else
                        r = 0x80, g = 0xD0, b = 0x30;
                }
                else if (dist2 < (radius * radius * 3 / 4))
                {
                    /* Mid ring */
                    if (hovered)
                        r = 0x70, g = 0xD0, b = 0x30;
                    else
                        r = 0x50, g = 0xB0, b = 0x18;
                }
                else
                {
                    /* Outer edge — darker */
                    if (hovered)
                        r = 0x50, g = 0xA0, b = 0x20;
                    else
                        r = 0x30, g = 0x80, b = 0x10;
                }
                fb_putpixel(cx + dx, cy + dy, FB_RGB(r, g, b));
            }
        }
    }

    /* Small Windows flag-like design in center */
    fb_fillrect(cx - 4, cy - 3, 4, 3, FB_RGB(0xFF, 0xCC, 0x00));
    fb_fillrect(cx, cy - 3, 4, 3, FB_RGB(0xE0, 0x40, 0x30));
    fb_fillrect(cx - 4, cy, 4, 3, FB_RGB(0x30, 0xA0, 0x30));
    fb_fillrect(cx, cy, 4, 3, FB_RGB(0x30, 0x70, 0xD0));

}

static void draw_taskbar(void)
{
    int tby = fb_info.height - GUI_TASK_HEIGHT;

    /* Aero glass taskbar background — dark translucent gradient */
    for (int row = 0; row < GUI_TASK_HEIGHT; row++)
    {
        uint8_t alpha = 200 + (uint32_t)(55 * row) / GUI_TASK_HEIGHT;
        uint32_t col = FB_RGB(0x20, 0x20, 0x28);
        fb_fillrect_alpha(0, tby + row, fb_info.width, 1, col, alpha);
    }

    /* Top glass highlight */
    fb_draw_hline(tby, 0, fb_info.width - 1, FB_RGB(0x50, 0x50, 0x58));

    /* Start Orb button */
    int orb_x = 4;
    int orb_y = tby + (GUI_TASK_HEIGHT - W7_ORB_SIZE) / 2;

    int mx = mouse_get_x_wrapper();
    int my = mouse_get_y_wrapper();
    int orb_hovered = (mx >= orb_x && mx < orb_x + W7_ORB_SIZE &&
                       my >= orb_y && my < orb_y + W7_ORB_SIZE);
    draw_orb_button(orb_x, orb_y, W7_ORB_SIZE, orb_hovered);

    /* System tray area */
    int tray_x = fb_info.width - W7_TRAY_W;
    int tray_y = tby + 2;

    /* Tray separator line */
    fb_draw_vline(tray_x, tby + 4, tby + GUI_TASK_HEIGHT - 5, FB_RGB(0x60, 0x60, 0x68));

    /* Clock */
    char time_buf[16];
    int time_len = 0;
    time_buf[time_len++] = '1';
    time_buf[time_len++] = '2';
    time_buf[time_len++] = ':';
    time_buf[time_len++] = '0';
    time_buf[time_len++] = '0';
    time_buf[time_len++] = ' ';
    time_buf[time_len++] = 'P';
    time_buf[time_len++] = 'M';
    time_buf[time_len] = 0;
    int time_x = tray_x + (W7_TRAY_W - time_len * FONT_WIDTH) / 2;
    fb_drawstring(time_x, tray_y + 4, time_buf, COL_WHITE, 0);

    /* Show desktop button (thin strip at far right) */
    int sdb_x = fb_info.width - 4;
    fb_fillrect(sdb_x, tby, 4, GUI_TASK_HEIGHT, FB_RGB(0x30, 0x30, 0x38));
    fb_draw_vline(sdb_x, tby + 2, tby + GUI_TASK_HEIGHT - 3, FB_RGB(0x70, 0x70, 0x78));

    /* Running application buttons */
    int bx = orb_x + W7_ORB_SIZE + 6;
    int max_bw = tray_x - bx - 4;

    for (int i = 0; i < num_windows; i++)
    {
        if (!windows[i].visible) continue;

        int bw = strlen(windows[i].title) * FONT_WIDTH + 20;
        if (bw > 160) bw = 160;
        if (bx + bw > max_bw)
        {
            int remaining = max_bw - bx;
            if (remaining < 40) break;
            bw = remaining;
        }

        int is_active = (i == active_window) && !windows[i].minimized;
        int btn_y = tby + 3;
        int btn_h = GUI_TASK_HEIGHT - 6;

        /* Aero glass button background */
        for (int row = 0; row < btn_h; row++)
        {
            uint8_t alpha = is_active ? 200 : 120;
            uint32_t col = is_active ? FB_RGB(0x50, 0x60, 0x70) : FB_RGB(0x40, 0x40, 0x48);
            fb_fillrect_alpha(bx, btn_y + row, bw, 1, col, alpha);
        }

        /* Active indicator line at top */
        if (is_active)
            fb_draw_hline(btn_y, bx + 2, bx + bw - 3, COL_W7_AERO_GLOW_ACT);
        else
            fb_draw_hline(btn_y, bx + 2, bx + bw - 3, FB_RGB(0x60, 0x60, 0x68));

        fb_drawstring(bx + 6, btn_y + 2, windows[i].title, COL_WHITE, 0);
        bx += bw + 3;
    }

    /* Mouse position in system tray area (small debug) */
    if (mouse_is_present_wrapper())
    {
        char pos_buf[16];
        int plen = 0;
        int px = mouse_get_x_wrapper();
        int py = mouse_get_y_wrapper();
        pos_buf[plen++] = '0' + px / 100 % 10;
        pos_buf[plen++] = '0' + px / 10 % 10;
        pos_buf[plen++] = '0' + px % 10;
        pos_buf[plen++] = ',';
        pos_buf[plen++] = '0' + py / 100 % 10;
        pos_buf[plen++] = '0' + py / 10 % 10;
        pos_buf[plen++] = '0' + py % 10;
        pos_buf[plen] = 0;
        fb_drawstring(tray_x + 2, tby + GUI_TASK_HEIGHT - FONT_HEIGHT - 4, pos_buf, FB_RGB(0xA0, 0xA0, 0xA0), 0);
    }
}

void gui_render(void)
{
    if (!initialized) return;

    fb_backbuffer_alloc();

    fb_clear(GUI_DESKTOP_COL);

    {
        int mmx = mouse_get_x_wrapper();
        int mmy = mouse_get_y_wrapper();
        for (int mi = 0; mi < num_menus; mi++)
        {
            menus[mi].hovered = -1;
            if (!menus[mi].is_open) continue;
            if (mmx < menus[mi].x || mmx >= menus[mi].x + GUI_MENU_DROPDOWN_W) continue;
            if (mmy < GUI_MENU_HEIGHT) continue;
            int item_h = FONT_HEIGHT + 4;
            int idx = (mmy - GUI_MENU_HEIGHT - 1) / item_h;
            if (idx >= 0 && idx < menus[mi].num_items)
                menus[mi].hovered = idx;
        }
    }

    if (start_menu_open)
    {
        int tby = fb_info.height - GUI_TASK_HEIGHT;
        int mmx = mouse_get_x_wrapper();
        int mmy = mouse_get_y_wrapper();
        start_menu_hovered = -1;

        int max_items = start_left_count > start_right_count ? start_left_count : start_right_count;
        int total_h = XP_SM_HEADER_H + max_items * XP_SM_ITEM_H + W7_SM_SEARCH_H + W7_SM_BOTTOM_H;
        int smy = tby - total_h;

        if (mmx >= 2 && mmx < 2 + XP_SM_TOTAL_W &&
            mmy >= smy && mmy < smy + total_h)
        {
            if (mmy < smy + XP_SM_HEADER_H)
                goto sm_hover_done;

            int sep_x = 2 + XP_SM_LEFT_W;
            int item_area_y = smy + XP_SM_HEADER_H;
            int bottom_y = smy + XP_SM_HEADER_H + max_items * XP_SM_ITEM_H;
            int search_area_y = bottom_y;
            int shutdown_area_y = search_area_y + W7_SM_SEARCH_H;
            int idx = (mmy - item_area_y) / XP_SM_ITEM_H;

            if (mmy >= shutdown_area_y)
            {
                /* Shut down button at bottom */
                int shutdown_x = 2 + XP_SM_TOTAL_W - 100;
                if (mmx >= shutdown_x && mmx < shutdown_x + 96)
                    start_menu_hovered = start_left_count + 1 + start_right_count;
            }
            else if (mmy >= search_area_y)
            {
                /* Search area — no hover */
            }
            else if (mmx < sep_x)
            {
                /* Left column: programs + "All Programs" */
                if (idx >= 0 && idx < start_left_count)
                    start_menu_hovered = idx;
                else if (idx == start_left_count)
                    start_menu_hovered = idx;
            }
            else
            {
                /* Right column */
                if (idx >= 0 && idx < start_right_count)
                    start_menu_hovered = start_left_count + 1 + idx;
            }
        }
    sm_hover_done: ;
    }

    draw_menu_bar();
    draw_desktop_icons();

    for (int i = 0; i < num_windows; i++)
    {
        if (windows[i].dragging)
        {
            int mx = mouse_get_x_wrapper();
            int my = mouse_get_y_wrapper();
            windows[i].drag_outline_x = mx - windows[i].drag_off_x;
            windows[i].drag_outline_y = my - windows[i].drag_off_y;
        }
    }

    for (int i = 0; i < num_windows; i++)
    {
        if (i != active_window && windows[i].visible && !windows[i].minimized)
            draw_window(i);
    }
    if (active_window >= 0 && windows[active_window].visible && !windows[active_window].minimized)
        draw_window(active_window);

    draw_start_menu();
    draw_taskbar();

    uint8_t buttons = mouse_get_buttons_wrapper();

    for (int i = 0; i < num_windows; i++)
    {
        if (windows[i].dragging && !(buttons & 1))
        {
            windows[i].x = windows[i].drag_outline_x;
            windows[i].y = windows[i].drag_outline_y;
            windows[i].dragging = 0;
        }
    }

    if ((buttons & 1) && !(prev_buttons & 1))
        handle_click();
    prev_buttons = buttons;

    draw_mouse_cursor();

    fb_blit();
}

void gui_set_active(int idx)
{
    if (idx >= 0 && idx < num_windows)
        active_window = idx;
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
