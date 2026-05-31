#include "types.h"
#include "winman.h"
#include "screen.h"
#include "string.h"
#include "io.h"
#include "mem.h"

// Mouse access
extern int mouse_get_x(void);
extern int mouse_get_y(void);
extern uint8_t mouse_get_buttons(void);
extern int mouse_is_present(void);

static uint16_t* vga = (uint16_t*)VGA_MEMORY;

static void update_hw_cursor(void);
static window_t windows[WINMAN_MAX_WINDOWS];
static int num_windows = 0;
static int active_window = -1;
static uint8_t prev_buttons = 0;
static int initialized = 0;

// Colors
#define COL_DESKTOP   1
#define COL_MENU_FG   15
#define COL_MENU_BG   1
#define COL_STATUS_FG 15
#define COL_STATUS_BG 1
#define COL_WIN_TL_FG 11
#define COL_WIN_TL_BG 1
#define COL_WIN_FG    7
#define COL_WIN_BG    0
#define COL_CURSOR_FG 15
#define COL_CURSOR_BG 4

static void cell(int x, int y, char c, uint8_t fg, uint8_t bg)
{
    if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT) return;
    vga[y * VGA_WIDTH + x] = (uint16_t)c | ((uint16_t)(fg | (bg << 4)) << 8);
}

static void fill(int x, int y, int w, int h, char c, uint8_t fg, uint8_t bg)
{
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            cell(x + col, y + row, c, fg, bg);
}

static void draw_menu_bar(void)
{
    fill(0, 0, VGA_WIDTH, 1, ' ', COL_MENU_FG, COL_MENU_BG);
    const char* items[] = {"File", "Edit", "View", "Help"};
    int x = 1;
    cell(x++, 0, ' ', COL_MENU_FG, COL_MENU_BG);
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; items[i][j]; j++)
            cell(x++, 0, items[i][j], 14, COL_MENU_BG);
        x++;
    }
    int tx = VGA_WIDTH - 17;
    const char* title = "BlueOS Windows";
    for (int i = 0; title[i]; i++)
        cell(tx++, 0, title[i], COL_MENU_FG, COL_MENU_BG);
}

static void draw_status_bar(void)
{
    fill(0, 24, VGA_WIDTH, 1, ' ', COL_STATUS_FG, COL_STATUS_BG);
    int mx = 0, my = 0;
    if (mouse_is_present())
    {
        mx = mouse_get_x();
        my = mouse_get_y();
    }
    char buf[32];
    int len = 0;
    const char* s = " Ready ";
    for (int i = 0; s[i]; i++) buf[len++] = s[i];

    int mc_x = mx / 8;
    int mc_y = my / 16;
    if (mc_x < 0) mc_x = 0;
    if (mc_x >= VGA_WIDTH) mc_x = VGA_WIDTH - 1;
    if (mc_y < 0) mc_y = 0;
    if (mc_y >= VGA_HEIGHT) mc_y = VGA_HEIGHT - 1;

    buf[len++] = ' ';
    buf[len++] = 'M';
    buf[len++] = 'o';
    buf[len++] = 'u';
    buf[len++] = 's';
    buf[len++] = 'e';
    buf[len++] = ':';
    buf[len++] = ' ';
    buf[len++] = '0' + mc_x / 100 % 10;
    buf[len++] = '0' + mc_x / 10 % 10;
    buf[len++] = '0' + mc_x % 10;
    buf[len++] = ',';
    buf[len++] = '0' + mc_y / 10 % 10;
    buf[len++] = '0' + mc_y % 10;
    buf[len] = 0;

    int sx = VGA_WIDTH - len - 1;
    for (int i = 0; buf[i]; i++)
        cell(sx + i, 24, buf[i], COL_STATUS_FG, COL_STATUS_BG);
}

static void draw_window(int idx)
{
    window_t* w = &windows[idx];
    if (!w->visible) return;

    int x = w->x, y = w->y;
    int ww = w->w, wh = w->h;
    int active = (idx == active_window);
    uint8_t tl_fg = active ? 15 : 7;
    uint8_t tl_bg = active ? 1 : 8;

    // Top border with title
    cell(x, y, BOX_TL2, tl_fg, tl_bg);
    cell(x + ww - 1, y, BOX_TR2, tl_fg, tl_bg);

    int tx = x + 2;
    for (int i = 0; w->title[i] && tx < x + ww - 6; i++)
        cell(tx++, y, w->title[i], tl_fg, tl_bg);
    for (; tx < x + ww - 4; tx++)
        cell(tx, y, ' ', tl_fg, tl_bg);

    cell(x + ww - 4, y, '[', tl_fg, tl_bg);
    cell(x + ww - 3, y, 0xFE, tl_fg, tl_bg);
    cell(x + ww - 2, y, ']', tl_fg, tl_bg);

    // Left and right borders
    for (int row = 1; row < wh - 1; row++)
    {
        cell(x, y + row, BOX_V2, w->fg, w->bg);
        cell(x + ww - 1, y + row, BOX_V2, w->fg, w->bg);
    }

    // Client area
    fill(x + 1, y + 1, ww - 2, wh - 2, ' ', w->fg, w->bg);

    // Content buffer
    if (w->content)
    {
        int cx = x + 1;
        int cy = y + 1;
        for (int row = 0; row < w->ch && row < wh - 2; row++)
        {
            for (int col = 0; col < w->cw && col < ww - 2; col++)
            {
                char c = w->content[row * w->cw + col];
                if (c)
                    cell(cx + col, cy + row, c, w->fg, w->bg);
            }
        }
    }

    // Cursor
    if (idx == active_window && w->content)
    {
        int cx = x + 1 + w->cursor_x;
        int cy = y + 1 + w->cursor_y;
        if (cx >= x + 1 && cx < x + ww - 1 && cy >= y + 1 && cy < y + wh - 1)
            cell(cx, cy, BLOCK, COL_CURSOR_FG, COL_CURSOR_BG);
    }

    // Bottom border
    cell(x, y + wh - 1, BOX_BL2, tl_fg, tl_bg);
    cell(x + ww - 1, y + wh - 1, BOX_BR2, tl_fg, tl_bg);
    for (int i = 1; i < ww - 1; i++)
        cell(x + i, y + wh - 1, BOX_H2, tl_fg, tl_bg);

    // Buttons
    for (int b = 0; b < w->num_buttons; b++)
    {
        win_button_t* btn = &w->buttons[b];
        int bx = x + 1 + btn->x;
        int by = y + 1 + btn->y;
        cell(bx, by, '[', btn->fg, btn->bg);
        for (int i = 0; btn->label[i]; i++)
            cell(bx + 1 + i, by, btn->label[i], btn->fg, btn->bg);
        cell(bx + 1 + strlen(btn->label), by, ']', btn->fg, btn->bg);
    }
}

static void draw_mouse_cursor(void)
{
    if (!mouse_is_present()) return;
    int mx = mouse_get_x() / 8;
    int my = mouse_get_y() / 16;
    if (mx < 0 || mx >= VGA_WIDTH || my < 0 || my >= VGA_HEIGHT) return;
    if (my == 0 || my == 24) return;

    uint16_t c = vga[my * VGA_WIDTH + mx];
    uint8_t fg = (c >> 12) & 0x0F;
    uint8_t bg = (c >> 8) & 0x0F;
    char ch = c & 0xFF;
    cell(mx, my, ch ? ch : ' ', bg, fg);
}

static void handle_click(void)
{
    int mx = mouse_get_x() / 8;
    int my = mouse_get_y() / 16;
    if (mx < 0 || mx >= VGA_WIDTH || my < 0 || my >= VGA_HEIGHT) return;

    // Check if clicked on a window
    for (int i = num_windows - 1; i >= 0; i--)
    {
        window_t* w = &windows[i];
        if (!w->visible) continue;
        if (mx < w->x || mx >= w->x + w->w) continue;
        if (my < w->y || my >= w->y + w->h) continue;

        // Clicked inside this window
        if (my == w->y && mx >= w->x + w->w - 4 && mx < w->x + w->w - 1)
        {
            w->visible = 0;
            if (active_window == i)
                active_window = -1;
            return;
        }

        if (my == w->y)
        {
            active_window = i;
            update_hw_cursor();
            return;
        }

        active_window = i;

        // Check buttons
        int cx = mx - (w->x + 1);
        int cy = my - (w->y + 1);
        for (int b = 0; b < w->num_buttons; b++)
        {
            win_button_t* btn = &w->buttons[b];
            if (cy == btn->y && cx >= btn->x && cx < btn->x + btn->w)
            {
                if (btn->on_click) btn->on_click(i, b);
                return;
            }
        }

        update_hw_cursor();
        return;
    }
}

static void update_hw_cursor(void)
{
    if (active_window >= 0 && active_window < num_windows)
    {
        window_t* w = &windows[active_window];
        if (w->visible)
        {
            int pos = (w->y + 1 + w->cursor_y) * VGA_WIDTH + (w->x + 1 + w->cursor_x);
            outb(0x3D4, 0x0F);
            outb(0x3D5, pos & 0xFF);
            outb(0x3D4, 0x0E);
            outb(0x3D5, (pos >> 8) & 0xFF);
            outb(0x3D4, 0x0A);
            outb(0x3D5, (inb(0x3D5) & ~0x20) | 0x0E);
            outb(0x3D4, 0x0B);
            outb(0x3D5, 0x0F);
            return;
        }
    }
    outb(0x3D4, 0x0A);
    outb(0x3D5, inb(0x3D5) | 0x20);
}

void winman_init(void)
{
    num_windows = 0;
    active_window = -1;
    initialized = 1;

    outb(0x3D4, 0x0A);
    outb(0x3D5, inb(0x3D5) | 0x20);

    winman_render();
}

int winman_create(const char* title, int x, int y, int w, int h)
{
    if (num_windows >= WINMAN_MAX_WINDOWS) return -1;
    if (w < 10 || h < 4) return -1;

    int idx = num_windows++;
    window_t* win = &windows[idx];

    int slen = strlen(title);
    if (slen >= (int)sizeof(win->title)) slen = sizeof(win->title) - 1;
    memcpy(win->title, title, slen);
    win->title[slen] = 0;

    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->visible = 1;
    win->fg = COL_WIN_FG;
    win->bg = COL_WIN_BG;
    win->title_fg = 15;
    win->title_bg = 1;
    win->cw = w - 2;
    win->ch = h - 2;
    win->cursor_x = 0;
    win->cursor_y = 0;
    win->num_buttons = 0;

    win->content = malloc(win->cw * win->ch);
    if (win->content)
        memset(win->content, ' ', win->cw * win->ch);

    active_window = idx;
    return idx;
}

void winman_putchar(int idx, char c)
{
    if (idx < 0 || idx >= num_windows) return;
    window_t* w = &windows[idx];
    if (!w->visible || !w->content) return;

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

void winman_puts(int idx, const char* str)
{
    for (int i = 0; str[i]; i++)
        winman_putchar(idx, str[i]);
}

void winman_clear(int idx)
{
    if (idx < 0 || idx >= num_windows) return;
    window_t* w = &windows[idx];
    if (w->content)
    {
        memset(w->content, ' ', w->cw * w->ch);
        w->cursor_x = 0;
        w->cursor_y = 0;
    }
}

int winman_add_button(int idx, const char* label, int bx, int by, int bw, void (*cb)(int, int))
{
    if (idx < 0 || idx >= num_windows) return -1;
    window_t* w = &windows[idx];
    if (w->num_buttons >= WINMAN_MAX_BUTTONS) return -1;

    int bi = w->num_buttons++;
    win_button_t* btn = &w->buttons[bi];
    btn->x = bx;
    btn->y = by;
    btn->w = bw;
    btn->fg = 15;
    btn->bg = 1;

    int slen = strlen(label);
    if (slen >= (int)sizeof(btn->label)) slen = sizeof(btn->label) - 1;
    memcpy(btn->label, label, slen);
    btn->label[slen] = 0;
    btn->on_click = cb;

    return bi;
}

void winman_render(void)
{
    if (!initialized) return;

    // Desktop
    fill(0, 1, VGA_WIDTH, 23, ' ', COL_DESKTOP, COL_DESKTOP);

    // Menu bar
    draw_menu_bar();

    // Status bar
    draw_status_bar();

    // Windows (active last for z-order)
    for (int i = 0; i < num_windows; i++)
    {
        if (i != active_window && windows[i].visible)
            draw_window(i);
    }
    if (active_window >= 0 && windows[active_window].visible)
        draw_window(active_window);

    // Mouse
    uint8_t buttons = mouse_get_buttons();
    if ((buttons & 1) && !(prev_buttons & 1))
        handle_click();
    prev_buttons = buttons;

    draw_mouse_cursor();
    update_hw_cursor();
}

void winman_set_active(int idx)
{
    if (idx >= 0 && idx < num_windows)
        active_window = idx;
}
