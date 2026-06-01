#include "types.h"
#include "string.h"
#include "mem.h"
#include "screen.h"
#include "fb.h"
#include "gui.h"
#include "font.h"

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

static void gui_terminal_putchar(char c)
{
    gui_putchar(gui_terminal_win, c);
}

int gui_create_terminal(const char* title, int w, int h)
{
    gui_terminal_win = gui_create(title, w, h);
    screen_set_redirect(gui_terminal_putchar);
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

static void draw_status_bar(void)
{
    fb_fillrect(0, fb_info.height - GUI_STATUS_HEIGHT, fb_info.width, GUI_STATUS_HEIGHT, COL_LIGHT_GRAY);
    draw_3d_rect(0, fb_info.height - GUI_STATUS_HEIGHT, fb_info.width, GUI_STATUS_HEIGHT, 1);

    char buf[64];
    int len = 0;
    const char* s = " Ready ";
    for (int i = 0; s[i]; i++) buf[len++] = s[i];

    if (mouse_is_present())
    {
        int mx = mouse_get_x();
        int my = mouse_get_y();
        buf[len++] = ' ';
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

    int sx = fb_info.width - len * FONT_WIDTH - 8;
    fb_drawstring(sx, fb_info.height - GUI_STATUS_HEIGHT + 2, buf, COL_BLACK, COL_LIGHT_GRAY);
}

static void gui_tile(void)
{
    int content_y = GUI_MENU_HEIGHT;
    int content_h = fb_info.height - GUI_MENU_HEIGHT - GUI_STATUS_HEIGHT;
    int content_w = fb_info.width;

    int visible_count = 0;
    for (int i = 0; i < num_windows; i++)
        if (windows[i].visible) visible_count++;

    if (visible_count == 0) return;

    int master_w = content_w * GUI_TILE_MASTER_RATIO / 100;
    int stack_w = content_w - master_w;
    int stack_count = visible_count - 1;

    int idx = 0;
    for (int i = 0; i < num_windows; i++)
    {
        if (!windows[i].visible) continue;

        if (idx == 0)
        {
            windows[i].x = 0;
            windows[i].y = content_y;
            windows[i].w = master_w;
            windows[i].h = content_h;
        }
        else
        {
            int stack_h = content_h / stack_count;
            windows[i].x = master_w;
            windows[i].y = content_y + (idx - 1) * stack_h;
            windows[i].w = stack_w;
            if (idx == stack_count)
                windows[i].h = content_h - (idx - 1) * stack_h;
            else
                windows[i].h = stack_h;
        }

        int new_cw = (windows[i].w - 2 * GUI_BORDER_WIDTH) / FONT_WIDTH;
        int new_ch = (windows[i].h - 2 * GUI_BORDER_WIDTH - GUI_TITLE_HEIGHT - 1) / FONT_HEIGHT;
        if (new_cw < 1) new_cw = 1;
        if (new_ch < 1) new_ch = 1;

        if (new_cw != windows[i].cw || new_ch != windows[i].ch)
        {
            char* new_content = malloc(new_cw * new_ch);
            if (new_content)
            {
                __asm__ volatile("cli");
                memset(new_content, ' ', new_cw * new_ch);
                if (windows[i].content)
                {
                    int copy_w = windows[i].cw < new_cw ? windows[i].cw : new_cw;
                    int copy_h = windows[i].ch < new_ch ? windows[i].ch : new_ch;
                    for (int r = 0; r < copy_h; r++)
                        memcpy(new_content + r * new_cw, windows[i].content + r * windows[i].cw, copy_w);
                    free(windows[i].content);
                }
                windows[i].content = new_content;
                windows[i].cw = new_cw;
                windows[i].ch = new_ch;
                __asm__ volatile("sti");
            }
        }

        idx++;
    }
}

static void draw_window_border(gui_window_t* w, int active)
{
    uint32_t col = active ? COL_WIN_BLUE2 : COL_DARK_GRAY;
    int x = w->x, y = w->y, ww = w->w, wh = w->h;

    for (int b = 0; b < GUI_BORDER_WIDTH; b++)
    {
        fb_draw_hline(y + b, x + b, x + ww - 1 - b, col);
        fb_draw_hline(y + wh - 1 - b, x + b, x + ww - 1 - b, col);
        fb_draw_vline(x + b, y + b, y + wh - 1 - b, col);
        fb_draw_vline(x + ww - 1 - b, y + b, y + wh - 1 - b, col);
    }
}

static void draw_window_title_bar(gui_window_t* w, int active)
{
    int tx = w->x + GUI_BORDER_WIDTH;
    int ty = w->y + GUI_BORDER_WIDTH;
    int tw = w->w - 2 * GUI_BORDER_WIDTH;

    uint32_t bg = active ? COL_WIN_BLUE : COL_DARK_GRAY;
    uint32_t fg = active ? COL_WHITE : COL_LIGHT_GRAY;

    fb_fillrect(tx, ty, tw, GUI_TITLE_HEIGHT, bg);
    fb_drawstring(tx + 3, ty + 1, w->title, fg, bg);

    fb_draw_hline(ty + GUI_TITLE_HEIGHT, tx, tx + tw - 1, COL_DARK_GRAY);
}

static void draw_window_content(gui_window_t* w)
{
    int cx = w->x + GUI_BORDER_WIDTH;
    int cy = w->y + GUI_BORDER_WIDTH + GUI_TITLE_HEIGHT + 1;
    int cw = w->w - 2 * GUI_BORDER_WIDTH;
    int ch = w->h - 2 * GUI_BORDER_WIDTH - GUI_TITLE_HEIGHT - 1;

    fb_fillrect(cx, cy, cw, ch, COL_WHITE);

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

static void draw_window(int idx)
{
    gui_window_t* w = &windows[idx];
    if (!w->visible) return;

    int active = (idx == active_window);

    draw_window_border(w, active);
    draw_window_title_bar(w, active);
    draw_window_content(w);

    for (int b = 0; b < w->num_buttons; b++)
    {
        gui_button_t* btn = &w->buttons[b];
        int bx = w->x + GUI_BORDER_WIDTH + btn->x * FONT_WIDTH;
        int by = w->y + GUI_BORDER_WIDTH + GUI_TITLE_HEIGHT + 1 + btn->y * FONT_HEIGHT;
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

static int find_window_at(int mx, int my)
{
    if (my < GUI_MENU_HEIGHT) return -1;
    if ((uint32_t)my >= fb_info.height - GUI_STATUS_HEIGHT) return -1;

    for (int i = 0; i < num_windows; i++)
    {
        if (!windows[i].visible) continue;
        if (mx >= windows[i].x && mx < windows[i].x + windows[i].w &&
            my >= windows[i].y && my < windows[i].y + windows[i].h)
            return i;
    }
    return -1;
}

static void handle_click(void)
{
    int mx = mouse_get_x();
    int my = mouse_get_y();
    if (mx < 0 || (uint32_t)mx >= fb_info.width || my < 0 || (uint32_t)my >= fb_info.height)
        return;

    if (handle_menu_click(mx, my))
        return;

    int idx = find_window_at(mx, my);
    if (idx < 0) return;

    if (idx > 0)
    {
        gui_window_t tmp = windows[0];
        windows[0] = windows[idx];
        windows[idx] = tmp;
        idx = 0;
    }

    active_window = idx;
    gui_window_t* w = &windows[idx];

    int cx = mx - (w->x + GUI_BORDER_WIDTH);
    int cy = my - (w->y + GUI_BORDER_WIDTH + GUI_TITLE_HEIGHT + 1);
    if (cy < 0) return;

    for (int b = 0; b < w->num_buttons; b++)
    {
        gui_button_t* btn = &w->buttons[b];
        int bx = btn->x * FONT_WIDTH;
        int by = btn->y * FONT_HEIGHT;
        if (cx >= bx && cx < bx + btn->w * FONT_WIDTH &&
            cy >= by && cy < by + FONT_HEIGHT + 4)
        {
            if (btn->on_click) btn->on_click(idx, b);
            return;
        }
    }

    if (w->on_content_click)
    {
        w->on_content_click(idx, mx, my);
        return;
    }

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

    win->x = 0;
    win->y = 0;
    win->w = w;
    win->h = h;
    win->visible = 1;
    win->cw = (w - 2 * GUI_BORDER_WIDTH) / FONT_WIDTH;
    win->ch = (h - 2 * GUI_BORDER_WIDTH - GUI_TITLE_HEIGHT - 1) / FONT_HEIGHT;
    win->cursor_x = 0;
    win->cursor_y = 0;
    win->num_buttons = 0;
    win->event_head = 0;
    win->event_tail = 0;

    if (win->cw < 1) win->cw = 1;
    if (win->ch < 1) win->ch = 1;

    win->content = malloc(win->cw * win->ch);
    if (win->content)
        memset(win->content, ' ', win->cw * win->ch);

    active_window = idx;
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

void gui_render(void)
{
    if (!initialized) return;

    fb_backbuffer_alloc();

    fb_clear(GUI_DESKTOP_COL);

    gui_tile();

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

    draw_menu_bar();

    for (int i = 0; i < num_windows; i++)
    {
        if (i != active_window && windows[i].visible)
            draw_window(i);
    }
    if (active_window >= 0 && windows[active_window].visible)
        draw_window(active_window);

    draw_status_bar();

    uint8_t buttons = mouse_get_buttons();

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
    if (x) *x = windows[win_id].x + GUI_BORDER_WIDTH;
    if (y) *y = windows[win_id].y + GUI_BORDER_WIDTH;
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
