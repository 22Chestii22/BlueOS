#include "types.h"
#include "string.h"
#include "mem.h"
#include "screen.h"
#include "fb.h"
#include "gui.h"
#include "font.h"
#include "pe.h"

extern int mouse_get_x(void);
extern int mouse_get_y(void);
extern uint8_t mouse_get_buttons(void);
extern int mouse_is_present(void);

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
static int start_submenu_open = 0;
static int start_submenu_hovered = -1;

static const char* start_items[GUI_MAX_START_ITEMS] = {
    "Programs",
    "Run...",
    "Help",
    "About BlueOS",
    "Exit",
    0, 0, 0
};
static int start_num_items = 5;

static const char* submenu_items[GUI_MAX_START_ITEMS] = {
    "Scout",
    "CMD",
    "RENDER",
    0, 0, 0, 0, 0
};
static int submenu_num_items = 3;

static const char* submenu_paths[GUI_MAX_START_ITEMS] = {
    "\\SYSTEM\\PROGRAMS\\SCOUT.EXE",
    "\\SYSTEM\\PROGRAMS\\CMD.EXE",
    "\\SYSTEM\\PROGRAMS\\RENDER.EXE",
    0, 0, 0, 0, 0
};

typedef struct {
    const char* label;
    const char* path;
    int x, y, w, h;
} desktop_icon_t;

static desktop_icon_t desktop_icons[] = {
    {"Scout", "\\SYSTEM\\PROGRAMS\\SCOUT.EXE", 20, 60, 64, 72},
    {"CMD",   "\\SYSTEM\\PROGRAMS\\CMD.EXE",   100, 60, 64, 72},
};
static int num_desktop_icons = 2;

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
    uint32_t light = raised ? COL_WHITE : COL_DARK_GRAY;
    uint32_t dark = raised ? COL_DARK_GRAY : COL_WHITE;
    fb_draw_hline(y, x, x + w - 1, light);
    fb_draw_vline(x, y, y + h - 1, light);
    fb_draw_hline(y + h - 1, x, x + w - 1, dark);
    fb_draw_vline(x + w - 1, y, y + h - 1, dark);
}

static void draw_win3d_rect(int x, int y, int w, int h, int raised)
{
    uint32_t hilite = raised ? COL_WHITE : FB_RGB(64,64,64);
    uint32_t light = raised ? COL_LIGHT_GRAY : COL_DARK_GRAY;
    uint32_t dark = raised ? COL_DARK_GRAY : COL_LIGHT_GRAY;
    uint32_t shadow = raised ? FB_RGB(64,64,64) : COL_WHITE;
    fb_draw_hline(y, x, x + w - 1, hilite);
    fb_draw_vline(x, y, y + h - 1, hilite);
    fb_draw_hline(y + 1, x + 1, x + w - 2, light);
    fb_draw_vline(x + 1, y + 1, y + h - 2, light);
    fb_draw_hline(y + h - 1, x, x + w - 1, shadow);
    fb_draw_vline(x + w - 1, y, y + h - 1, shadow);
    fb_draw_hline(y + h - 2, x + 1, x + w - 2, dark);
    fb_draw_vline(x + w - 2, y + 1, y + h - 2, dark);
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

    fb_fillrect(dx, dy, GUI_MENU_DROPDOWN_W, dh, COL_WHITE);
    draw_3d_rect(dx, dy, GUI_MENU_DROPDOWN_W, dh, 0);

    for (int i = 0; i < menu->num_items; i++)
    {
        int iy = dy + 1 + i * item_h;
        uint32_t bg = COL_WHITE;
        uint32_t fg = menu->items[i].enabled ? COL_BLACK : COL_LIGHT_GRAY;

        if (menu->hovered == i && menu->items[i].enabled)
        {
            bg = COL_WIN_BLUE;
            fg = COL_WHITE;
        }

        fb_fillrect(dx + 1, iy, GUI_MENU_DROPDOWN_W - 2, item_h, bg);
        fb_drawstring(dx + 3, iy + 2, menu->items[i].label, fg, bg);
    }
}

static void draw_menu_bar(void)
{
    fb_fillrect(0, 0, fb_info.width, GUI_MENU_HEIGHT, COL_LIGHT_GRAY);
    draw_3d_rect(0, 0, fb_info.width, GUI_MENU_HEIGHT, 0);

    int x = 4;
    for (int i = 0; i < num_menus; i++)
    {
        menus[i].x = x;
        int label_w = strlen(menus[i].label) * FONT_WIDTH;

        uint32_t bg = menus[i].is_open ? COL_WIN_BLUE : COL_LIGHT_GRAY;
        uint32_t fg = menus[i].is_open ? COL_WHITE : COL_BLACK;

        fb_fillrect(x - 2, 0, label_w + 4, GUI_MENU_HEIGHT, bg);
        fb_drawstring(x, 2, menus[i].label, fg, bg);

        x += label_w + 8;

        if (menus[i].is_open)
            draw_menu_dropdown(&menus[i]);
    }

    const char* title = "BlueOS";
    int tx = fb_info.width - strlen(title) * FONT_WIDTH - 6;
    fb_drawstring(tx, 2, title, COL_BLACK, COL_LIGHT_GRAY);
}

static void draw_desktop_icons(void)
{
    for (int i = 0; i < num_desktop_icons; i++)
    {
        desktop_icon_t* di = &desktop_icons[i];
        fb_fillrect(di->x, di->y, di->w, di->h, GUI_DESKTOP_COL);
        draw_3d_rect(di->x, di->y, di->w, di->h, 1);
        int dx = di->x + (di->w - 6 * FONT_WIDTH) / 2;
        if (dx < di->x) dx = di->x + 1;
        int dy = di->y + 2;
        fb_drawstring(dx, dy, di->label, COL_WHITE, GUI_DESKTOP_COL);
    }
}

static void draw_start_submenu(void)
{
    if (!start_submenu_open) return;

    int tby = fb_info.height - GUI_TASK_HEIGHT;
    int mh = start_num_items * (FONT_HEIGHT + 4) + 4;
    int my = tby - mh;
    int item_h = FONT_HEIGHT + 4;
    int smh = submenu_num_items * item_h + 4;
    int smx = 2 + GUI_START_DROPDOWN_W;
    int smy = my + 2;

    fb_fillrect(smx, smy, GUI_START_DROPDOWN_W, smh, COL_LIGHT_GRAY);
    draw_win3d_rect(smx, smy, GUI_START_DROPDOWN_W, smh, 1);

    for (int i = 0; i < submenu_num_items; i++)
    {
        if (!submenu_items[i]) continue;
        int iy = smy + 2 + i * item_h;
        uint32_t bg = COL_WHITE;
        uint32_t fg = COL_BLACK;

        if (start_submenu_hovered == i)
        {
            bg = COL_WIN_BLUE2;
            fg = COL_WHITE;
        }

        fb_fillrect(smx + 2, iy, GUI_START_DROPDOWN_W - 4, item_h, bg);
        fb_drawstring(smx + 6, iy + 2, submenu_items[i], fg, bg);
    }
}

static void draw_start_menu(void)
{
    if (!start_menu_open) return;

    int tby = fb_info.height - GUI_TASK_HEIGHT;
    int item_h = FONT_HEIGHT + 4;
    int mh = start_num_items * item_h + 4;
    int mx = 2;
    int my = tby - mh;

    fb_fillrect(mx, my, GUI_START_DROPDOWN_W, mh, COL_LIGHT_GRAY);
    draw_win3d_rect(mx, my, GUI_START_DROPDOWN_W, mh, 1);

    for (int i = 0; i < start_num_items; i++)
    {
        if (!start_items[i]) continue;
        int iy = my + 2 + i * item_h;
        uint32_t bg = COL_WHITE;
        uint32_t fg = COL_BLACK;

        if (start_menu_hovered == i)
        {
            bg = COL_WIN_BLUE2;
            fg = COL_WHITE;
        }

        fb_fillrect(mx + 2, iy, GUI_START_DROPDOWN_W - 4, item_h, bg);
        fb_drawstring(mx + 6, iy + 2, start_items[i], fg, bg);

        if (i == 0)
            fb_drawstring(mx + GUI_START_DROPDOWN_W - 14, iy + 2, ">", fg, bg);
    }

    draw_start_submenu();
}

static void draw_window_title_bar(gui_window_t* w, int active)
{
    uint32_t bg = active ? COL_WIN_BLUE2 : COL_DARK_GRAY;
    uint32_t fg = active ? COL_WHITE : COL_LIGHT_GRAY;

    fb_fillrect(w->x, w->y, w->w, GUI_TITLE_HEIGHT, bg);

    if (active)
    {
        fb_draw_hline(w->y, w->x, w->x + w->w - 1, FB_RGB(0x66,0x88,0xFF));
        fb_draw_hline(w->y + GUI_TITLE_HEIGHT - 1, w->x, w->x + w->w - 1, FB_RGB(0,0,0x55));
    }
    else
    {
        fb_draw_hline(w->y, w->x, w->x + w->w - 1, COL_LIGHT_GRAY);
        fb_draw_hline(w->y + GUI_TITLE_HEIGHT - 1, w->x, w->x + w->w - 1, COL_BLACK);
    }

    fb_drawstring(w->x + 3, w->y + 2, w->title, fg, bg);

    int cap_y = w->y + 2;

    if (!w->minimized)
    {
        int min_x = w->x + w->w - 33;
        fb_fillrect(min_x + 1, cap_y, 14, 14, COL_LIGHT_GRAY);
        draw_3d_rect(min_x + 1, cap_y, 14, 14, 1);
        fb_draw_hline(cap_y + 11, min_x + 4, min_x + 10, COL_BLACK);
    }

    int close_x = w->x + w->w - 17;
    fb_fillrect(close_x + 1, cap_y, 14, 14, COL_LIGHT_GRAY);
    draw_3d_rect(close_x + 1, cap_y, 14, 14, 1);
    fb_drawstring(close_x + 4, cap_y + 1, "X", COL_BLACK, COL_LIGHT_GRAY);
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

    fb_fillrect(w->x + 1, w->y + GUI_TITLE_HEIGHT + 1, w->w - 2, w->h - GUI_TITLE_HEIGHT - 2, COL_LIGHT_GRAY);

    draw_window_title_bar(w, active);

    draw_3d_rect(w->x, w->y, w->w, w->h, 1);

    draw_window_content(w);

    fb_draw_hline(w->y + w->h - 1, w->x, w->x + w->w - 1, COL_DARK_GRAY);

    for (int b = 0; b < w->num_buttons; b++)
    {
        gui_button_t* btn = &w->buttons[b];
        int bx = w->x + 1 + btn->x * FONT_WIDTH;
        int by = w->y + GUI_TITLE_HEIGHT + 1 + btn->y * FONT_HEIGHT;
        int bw = btn->w * FONT_WIDTH;
        int bh = FONT_HEIGHT + 4;

        fb_fillrect(bx, by, bw, bh, COL_LIGHT_GRAY);
        draw_3d_rect(bx, by, bw, bh, 1);
        fb_drawstring(bx + 2, by + 2, btn->label, COL_BLACK, COL_LIGHT_GRAY);
    }
}

static void draw_mouse_cursor(void)
{
    if (!mouse_is_present()) return;
    int mx = mouse_get_x();
    int my = mouse_get_y();
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

static void handle_submenu_click(int mx, int my)
{
    int tby = fb_info.height - GUI_TASK_HEIGHT;
    int item_h = FONT_HEIGHT + 4;
    int mh = start_num_items * item_h + 4;
    int my2 = tby - mh;
    int smh = submenu_num_items * item_h + 4;
    int smx = 2 + GUI_START_DROPDOWN_W;
    int smy = my2 + 2;

    if (mx < smx || mx >= smx + GUI_START_DROPDOWN_W ||
        my < smy || my >= smy + smh)
        return;

    int idx = (my - smy - 2) / item_h;
    if (idx < 0 || idx >= submenu_num_items) return;
    if (!submenu_items[idx]) return;

    start_menu_open = 0;
    start_submenu_open = 0;

    if (submenu_paths[idx])
        pe_spawn(submenu_paths[idx]);
}

static void handle_start_menu_click(int mx, int my)
{
    if (!start_menu_open) return;

    if (start_submenu_open)
    {
        int tby = fb_info.height - GUI_TASK_HEIGHT;
        int item_h = FONT_HEIGHT + 4;
        int mh = start_num_items * item_h + 4;
        int my2 = tby - mh;
        int smh = submenu_num_items * item_h + 4;
        int smx = 2 + GUI_START_DROPDOWN_W;
        int smy = my2 + 2;

        if (mx >= smx && mx < smx + GUI_START_DROPDOWN_W &&
            my >= smy && my < smy + smh)
        {
            handle_submenu_click(mx, my);
            return;
        }
    }

    int tby = fb_info.height - GUI_TASK_HEIGHT;
    int item_h = FONT_HEIGHT + 4;
    int mh = start_num_items * item_h + 4;
    int smx = 2;
    int smy = tby - mh;

    if (mx < smx || mx >= smx + GUI_START_DROPDOWN_W ||
        my < smy || my >= smy + mh)
    {
        start_menu_open = 0;
        start_submenu_open = 0;
        return;
    }

    int idx = (my - smy - 2) / item_h;
    if (idx < 0 || idx >= start_num_items) return;
    if (!start_items[idx]) return;

    if (strcmp(start_items[idx], "Programs") == 0)
    {
        start_submenu_open = !start_submenu_open;
        return;
    }

    start_submenu_open = 0;
    start_menu_open = 0;

    if (strcmp(start_items[idx], "Exit") == 0)
        cmd_should_exit = 1;
    else if (strcmp(start_items[idx], "About BlueOS") == 0)
    {
        if (gui_terminal_win >= 0)
            gui_puts(gui_terminal_win, "\nBlueOS x86_64 v1.0 - Win95-style GUI\n\n");
    }
    else if (strcmp(start_items[idx], "Run...") == 0)
    {
        if (gui_terminal_win >= 0)
            gui_puts(gui_terminal_win, "\nType a program name:\n");
    }
    else if (strcmp(start_items[idx], "Help") == 0)
    {
        if (gui_terminal_win >= 0)
            gui_puts(gui_terminal_win, "\nBlueOS Help\n  Start menu for programs\n  Desktop icons\n  Window minimize/close\n\n");
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

    int start_x = 2;
    int start_w = GUI_START_BUTTON_W;
    if (mx >= start_x && mx < start_x + start_w)
    {
        if (start_menu_open)
        {
            start_submenu_open = 0;
            start_menu_open = 0;
        }
        else
        {
            start_submenu_open = 0;
            start_menu_open = 1;
        }
        return 1;
    }

    int bx = start_x + start_w + 4;
    int sinfo_w = mouse_is_present() ? 10 * FONT_WIDTH + 8 : 0;
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
    int mx = mouse_get_x();
    int my = mouse_get_y();
    if (mx < 0 || (uint32_t)mx >= fb_info.width || my < 0 || (uint32_t)my >= fb_info.height)
        return;

    if (start_menu_open)
    {
        int tby = fb_info.height - GUI_TASK_HEIGHT;
        int item_h = FONT_HEIGHT + 4;
        int mh = start_num_items * item_h + 4;
        int smx = 2;
        int smy = tby - mh;

        int in_main_menu = (mx >= smx && mx < smx + GUI_START_DROPDOWN_W &&
                            my >= smy && my < smy + mh);

        int in_submenu = 0;
        if (start_submenu_open)
        {
            int smh2 = submenu_num_items * item_h + 4;
            int smx2 = 2 + GUI_START_DROPDOWN_W;
            int smy2 = smy + 2;
            in_submenu = (mx >= smx2 && mx < smx2 + GUI_START_DROPDOWN_W &&
                          my >= smy2 && my < smy2 + smh2);
        }

        if (in_main_menu || in_submenu)
        {
            handle_start_menu_click(mx, my);
            return;
        }

        int start_x = 2;
        int start_w = GUI_START_BUTTON_W;
        if (my >= tby && mx >= start_x && mx < start_x + start_w)
        {
            start_submenu_open = 0;
            start_menu_open = 0;
            return;
        }

        start_submenu_open = 0;
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
                w->event_queue[w->event_tail].buttons = mouse_get_buttons();
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
    printf("[GUI] ensure_pixels win=%d pw=%d ph=%d alloc=%u\n", win_id, w->pw, w->ph, alloc_size);
    w->pixels = malloc(alloc_size);
    if (w->pixels)
    {
        memset(w->pixels, 0xFF, alloc_size);
        printf("[GUI] pixel buffer allocated OK at 0x%x\n", (uint32_t)(uint64_t)w->pixels);
    }
    else
    {
        printf("[GUI] pixel buffer ALLOCATION FAILED!\n");
    }
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
    start_submenu_open = 0;
    start_submenu_hovered = -1;
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

static void draw_taskbar(void)
{
    int tby = fb_info.height - GUI_TASK_HEIGHT;

    fb_fillrect(0, tby, fb_info.width, GUI_TASK_HEIGHT, COL_LIGHT_GRAY);
    fb_draw_hline(tby, 0, fb_info.width - 1, COL_WHITE);
    fb_draw_vline(0, tby, fb_info.height - 1, COL_WHITE);
    fb_draw_vline(fb_info.width - 1, tby, fb_info.height - 1, COL_DARK_GRAY);

    int start_x = 2;
    int start_y = tby + 2;
    int start_w = GUI_START_BUTTON_W;
    int start_h = GUI_TASK_HEIGHT - 4;

    uint32_t start_bg = start_menu_open ? FB_RGB(0,0,0x80) : COL_LIGHT_GRAY;
    uint32_t start_fg = start_menu_open ? COL_WHITE : COL_BLACK;
    fb_fillrect(start_x, start_y, start_w, start_h, start_bg);
    draw_3d_rect(start_x, start_y, start_w, start_h, !start_menu_open);
    fb_drawstring(start_x + 6, start_y + 2, "Start", start_fg, start_bg);

    char buf[64];
    int len = 0;

    if (mouse_is_present())
    {
        int mx = mouse_get_x();
        int my = mouse_get_y();
        buf[len++] = 'M';
        buf[len++] = ':';
        int t = mx;
        if (t >= 100) { buf[len++] = '0' + t / 100; t %= 100; }
        buf[len++] = '0' + t / 10;
        buf[len++] = '0' + t % 10;
        buf[len++] = ',';
        t = my;
        if (t >= 100) { buf[len++] = '0' + t / 100; t %= 100; }
        buf[len++] = '0' + t / 10;
        buf[len++] = '0' + t % 10;
    }
    buf[len] = 0;
    int sinfo_x = fb_info.width - len * FONT_WIDTH - 6;
    fb_drawstring(sinfo_x, tby + 4, buf, COL_BLACK, COL_LIGHT_GRAY);

    int bx = start_x + start_w + 4;
    int sinfo_w = (len > 0 ? len * FONT_WIDTH + 8 : 0);
    int max_buttons_w = fb_info.width - sinfo_w - bx - 4;

    for (int i = 0; i < num_windows; i++)
    {
        if (!windows[i].visible) continue;

        int bw = strlen(windows[i].title) * FONT_WIDTH + 14;
        if (bw > 180) bw = 180;
        if (bx + bw > max_buttons_w)
        {
            int remaining = max_buttons_w - bx;
            if (remaining < 30) break;
            bw = remaining;
        }

        int is_active = (i == active_window) && !windows[i].minimized;
        int is_min = windows[i].minimized;

        fb_fillrect(bx, tby + 2, bw, GUI_TASK_HEIGHT - 4, COL_LIGHT_GRAY);

        if (is_active)
        {
            fb_draw_hline(tby + 2, bx, bx + bw - 1, COL_DARK_GRAY);
            fb_draw_vline(bx, tby + 2, tby + GUI_TASK_HEIGHT - 3, COL_DARK_GRAY);
            fb_draw_hline(tby + GUI_TASK_HEIGHT - 3, bx, bx + bw - 1, COL_WHITE);
            fb_draw_vline(bx + bw - 1, tby + 2, tby + GUI_TASK_HEIGHT - 3, COL_WHITE);
        }
        else if (is_min)
        {
            fb_draw_hline(tby + 2, bx, bx + bw - 1, COL_LIGHT_GRAY);
            fb_draw_vline(bx, tby + 2, tby + GUI_TASK_HEIGHT - 3, COL_LIGHT_GRAY);
            fb_draw_hline(tby + GUI_TASK_HEIGHT - 3, bx, bx + bw - 1, COL_LIGHT_GRAY);
            fb_draw_vline(bx + bw - 1, tby + 2, tby + GUI_TASK_HEIGHT - 3, COL_LIGHT_GRAY);
        }
        else
        {
            fb_draw_hline(tby + 2, bx, bx + bw - 1, COL_WHITE);
            fb_draw_vline(bx, tby + 2, tby + GUI_TASK_HEIGHT - 3, COL_WHITE);
            fb_draw_hline(tby + GUI_TASK_HEIGHT - 3, bx, bx + bw - 1, COL_DARK_GRAY);
            fb_draw_vline(bx + bw - 1, tby + 2, tby + GUI_TASK_HEIGHT - 3, COL_DARK_GRAY);
        }

        fb_drawstring(bx + 4, tby + 4, windows[i].title, COL_BLACK, COL_LIGHT_GRAY);
        bx += bw + 2;
    }
}

void gui_render(void)
{
    if (!initialized) return;

    fb_backbuffer_alloc();

    fb_clear(GUI_DESKTOP_COL);

    {
        int mmx = mouse_get_x();
        int mmy = mouse_get_y();
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
        int item_h = FONT_HEIGHT + 4;
        int mmx = mouse_get_x();
        int mmy = mouse_get_y();
        start_menu_hovered = -1;
        start_submenu_hovered = -1;
        int mh = start_num_items * item_h + 4;
        int smy = tby - mh;
        if (mmx >= 2 && mmx < 2 + GUI_START_DROPDOWN_W &&
            mmy >= smy && mmy < smy + mh)
        {
            int idx = (mmy - smy - 2) / item_h;
            if (idx >= 0 && idx < start_num_items)
            {
                start_menu_hovered = idx;
                if (idx == 0)
                    start_submenu_open = 1;
                else
                    start_submenu_open = 0;
            }
        }
        else
        {
            start_submenu_open = 0;
        }

        if (start_submenu_open)
        {
            int smx = 2 + GUI_START_DROPDOWN_W;
            int smy2 = smy + 2;
            int smh = submenu_num_items * item_h + 4;
            if (mmx >= smx && mmx < smx + GUI_START_DROPDOWN_W &&
                mmy >= smy2 && mmy < smy2 + smh)
            {
                int idx = (mmy - smy2 - 2) / item_h;
                if (idx >= 0 && idx < submenu_num_items)
                    start_submenu_hovered = idx;
            }
        }
    }

    draw_menu_bar();
    draw_desktop_icons();

    for (int i = 0; i < num_windows; i++)
    {
        if (windows[i].dragging)
        {
            int mx = mouse_get_x();
            int my = mouse_get_y();
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

    uint8_t buttons = mouse_get_buttons();

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
