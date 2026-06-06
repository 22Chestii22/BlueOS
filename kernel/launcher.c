#include "types.h"
#include "string.h"
#include "mem.h"
#include "fb.h"
#include "font.h"
#include "module.h"
#include "blu.h"
#include "gui.h"

#define LAUNCHER_MAX_RESULTS 8
#define LAUNCHER_SEARCH_LEN 64
#define LAUNCHER_BAR_W 500
#define LAUNCHER_BAR_H 36
#define LAUNCHER_ITEM_H 24

static int launcher_open = 0;
static volatile int launcher_hotkey_flag = 0;
static char search_text[LAUNCHER_SEARCH_LEN];
static int search_pos = 0;
static int selected_result = 0;
static int num_results = 0;

static int bar_x, bar_y, bar_w = LAUNCHER_BAR_W, bar_h = LAUNCHER_BAR_H;
static int item_h = LAUNCHER_ITEM_H;

typedef struct {
    const char* name;
    const char* path;
} launcher_app_t;

static launcher_app_t launcher_apps[] = {
    {"CMD", "\\SYSTEM\\PROGRAMS\\CMD.BLU"},
    {"RENDER", "\\SYSTEM\\PROGRAMS\\RENDER.BLU"},
    {"IDLE", "\\SYSTEM\\PROGRAMS\\IDLE.BLU"},
    {"EDIT", "\\SYSTEM\\PROGRAMS\\EDIT.BLU"},
    {"SCOUT", "\\SYSTEM\\PROGRAMS\\SCOUT.BLU"},
    {"TASKMAN", "\\SYSTEM\\PROGRAMS\\TASKMAN.BLU"},
    {NULL, NULL}
};
static int num_launcher_apps = 6;

static int launcher_results[LAUNCHER_MAX_RESULTS];

static void draw_outline(int x, int y, int w, int h, uint32_t color)
{
    fb_draw_hline(y, x, x + w - 1, color);
    fb_draw_hline(y + h - 1, x, x + w - 1, color);
    fb_draw_vline(x, y, y + h - 1, color);
    fb_draw_vline(x + w - 1, y, y + h - 1, color);
}

static void launcher_toggle_isr(void)
{
    launcher_hotkey_flag = 1;
}

void launcher_init(void)
{
    kernel_api.register_hotkey_callback(launcher_toggle_isr);
}

static void launcher_update_results(void)
{
    num_results = 0;
    if (search_pos == 0)
    {
        for (int i = 0; i < num_launcher_apps && num_results < LAUNCHER_MAX_RESULTS; i++)
            launcher_results[num_results++] = i;
        return;
    }
    for (int i = 0; i < num_launcher_apps && num_results < LAUNCHER_MAX_RESULTS; i++)
    {
        if (strstr(launcher_apps[i].name, search_text))
            launcher_results[num_results++] = i;
    }
}

static void launcher_process_key(char c)
{
    if (c == '\033')
    {
        launcher_open = 0;
        search_pos = 0;
        search_text[0] = 0;
        gui_mark_dirty(0, 0, fb_info.width, fb_info.height);
        return;
    }

    if (c == '\n')
    {
        if (num_results > 0 && selected_result >= 0 && selected_result < num_results)
        {
            int idx = launcher_results[selected_result];
            if (idx >= 0 && idx < num_launcher_apps && launcher_apps[idx].path)
            {
                launcher_open = 0;
                search_pos = 0;
                search_text[0] = 0;
                gui_mark_dirty(0, 0, fb_info.width, fb_info.height);
                blu_spawn(launcher_apps[idx].path);
            }
        }
        return;
    }

    if (c == '\b')
    {
        if (search_pos > 0)
        {
            search_pos--;
            search_text[search_pos] = 0;
        }
        gui_mark_dirty(0, 0, fb_info.width, fb_info.height);
        return;
    }

    if (search_pos < LAUNCHER_SEARCH_LEN - 1 && c >= ' ')
    {
        search_text[search_pos++] = c;
        search_text[search_pos] = 0;
        selected_result = 0;
        gui_mark_dirty(0, 0, fb_info.width, fb_info.height);
    }
}

void launcher_update(void)
{
    if (launcher_hotkey_flag)
    {
        launcher_hotkey_flag = 0;
        launcher_open = !launcher_open;
        if (launcher_open)
        {
            search_pos = 0;
            search_text[0] = 0;
            selected_result = 0;
        }
        gui_mark_dirty(0, 0, fb_info.width, fb_info.height);
    }

    if (launcher_open)
    {
        while (keyb_char_avail_wrapper())
        {
            char c = keyb_getchar_wrapper();
            if (c)
                launcher_process_key(c);
        }
    }
}

static int launcher_get_result_at(int mx, int my)
{
    if (num_results <= 0) return -1;
    if (mx < bar_x || mx >= bar_x + bar_w) return -1;
    int list_y = bar_y + bar_h + 4;
    if (my < list_y || my >= list_y + num_results * item_h) return -1;
    return (my - list_y) / item_h;
}

void launcher_handle_click(int mx, int my)
{
    if (!launcher_open) return;

    int idx = launcher_get_result_at(mx, my);
    if (idx >= 0 && idx < num_results)
    {
        int app_idx = launcher_results[idx];
        if (app_idx >= 0 && app_idx < num_launcher_apps && launcher_apps[app_idx].path)
        {
            launcher_open = 0;
            search_pos = 0;
            search_text[0] = 0;
            gui_mark_dirty(0, 0, fb_info.width, fb_info.height);
            blu_spawn(launcher_apps[app_idx].path);
        }
        return;
    }

    if (mx >= bar_x && mx < bar_x + bar_w && my >= bar_y && my < bar_y + bar_h)
        return;

    launcher_open = 0;
    search_pos = 0;
    search_text[0] = 0;
    gui_mark_dirty(0, 0, fb_info.width, fb_info.height);
}

void launcher_render(void)
{
    if (!launcher_open) return;

    launcher_update_results();

    int sw = fb_info.width;
    int sh = fb_info.height;

    fb_fillrect_alpha(0, 0, sw, sh, COL_BLACK, 80);

    bar_x = (sw - bar_w) / 2;
    bar_y = sh / 4;

    fb_fillrect(bar_x, bar_y, bar_w, bar_h, COL_WHITE);
    draw_outline(bar_x, bar_y, bar_w, bar_h, FB_RGB(0x00, 0x58, 0xEE));

    int tx = bar_x + 8;
    int ty = bar_y + (bar_h - FONT_HEIGHT) / 2;

    fb_drawchar(tx, ty, '>', FB_RGB(0x00, 0x80, 0x00), COL_WHITE);
    tx += FONT_WIDTH * 2;

    for (int i = 0; i < search_pos; i++)
    {
        fb_drawchar(tx, ty, search_text[i], COL_BLACK, COL_WHITE);
        tx += FONT_WIDTH;
    }

    {
        uint32_t* bb = fb_get_backbuffer();
        if (bb)
        {
            uint32_t stride = fb_info.pitch / 4;
            for (int row = 0; row < FONT_HEIGHT; row++)
            {
                int py = ty + row;
                if (py < 0 || (uint32_t)py >= sh) continue;
                for (int col = 0; col < FONT_WIDTH; col++)
                {
                    int px = tx + col;
                    if (px < 0 || (uint32_t)px >= sw) continue;
                    bb[py * stride + px] = ~bb[py * stride + px] & 0x00FFFFFF;
                }
            }
        }
    }

    if (num_results > 0)
    {
        int list_y = bar_y + bar_h + 4;
        int list_h = num_results * item_h;

        fb_fillrect(bar_x, list_y, bar_w, list_h, COL_WHITE);
        draw_outline(bar_x, list_y, bar_w, list_h, FB_RGB(0x7F, 0x9D, 0xB9));

        int mmx = mouse_get_x_wrapper();
        int mmy = mouse_get_y_wrapper();
        int hover_idx = launcher_get_result_at(mmx, mmy);
        if (hover_idx >= 0 && hover_idx < num_results)
            selected_result = hover_idx;

        for (int i = 0; i < num_results; i++)
        {
            int app_idx = launcher_results[i];
            int iy = list_y + i * item_h;
            uint32_t bg = (i == selected_result) ? FB_RGB(0x31, 0x6A, 0xC5) : COL_WHITE;
            uint32_t fg = (i == selected_result) ? COL_WHITE : COL_BLACK;
            fb_fillrect(bar_x + 2, iy, bar_w - 4, item_h, bg);
            fb_drawstring(bar_x + 8, iy + (item_h - FONT_HEIGHT) / 2, launcher_apps[app_idx].name, fg, bg);
        }
    }
    else if (search_pos > 0)
    {
        int list_y = bar_y + bar_h + 4;
        fb_drawstring(bar_x + 8, list_y + 4, "No matching programs found", FB_RGB(0x80, 0x80, 0x80), 0);
    }
}

int launcher_is_open(void)
{
    return launcher_open;
}
