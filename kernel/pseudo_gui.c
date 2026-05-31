#include "types.h"
#include "screen.h"
#include "string.h"

#define P_VIEW_X 1
#define P_VIEW_Y 3
#define P_VIEW_W 78
#define P_VIEW_H 20

#define BG_BLUE   (1 << 4)
#define FG_WHITE  COLOR_WHITE
#define FG_CYAN   COLOR_CYAN
#define FG_GREY   COLOR_LIGHT_GREY

#define CHAR_TL 0xDA
#define CHAR_TR 0xBF
#define CHAR_BL 0xC0
#define CHAR_BR 0xD9
#define CHAR_H  0xC4
#define CHAR_V  0xB3
#define CHAR_ML 0xC3
#define CHAR_MR 0xB4

static void put_cell(int x, int y, char c, uint8_t color)
{
    uint16_t* buf = (uint16_t*)VGA_MEMORY;
    buf[y * VGA_WIDTH + x] = (uint16_t)c | ((uint16_t)color << 8);
}

static void draw_h_line(int x, int y, int w, char c, uint8_t color)
{
    for (int i = 0; i < w; i++)
        put_cell(x + i, y, c, color);
}

static void draw_v_line(int x, int y, int h, char c, uint8_t color)
{
    for (int i = 0; i < h; i++)
        put_cell(x, y + i, c, color);
}

static void draw_bar(int y, uint8_t color)
{
    for (int x = 0; x < VGA_WIDTH; x++)
        put_cell(x, y, ' ', color);
}

void pseudo_gui_init(void)
{
    uint8_t border_color = FG_CYAN | BG_BLUE;
    uint8_t title_color = FG_WHITE | BG_BLUE;
    uint8_t title_bg = BG_BLUE;

    draw_bar(0, BG_BLUE);

    put_cell(P_VIEW_X - 1, 0, CHAR_TL, border_color);
    draw_h_line(P_VIEW_X, 0, P_VIEW_W, CHAR_H, border_color);
    put_cell(P_VIEW_X + P_VIEW_W, 0, CHAR_TR, border_color);

    draw_bar(1, title_bg);
    put_cell(P_VIEW_X - 1, 1, CHAR_V, border_color);
    put_cell(P_VIEW_X + P_VIEW_W, 1, CHAR_V, border_color);

    const char* title = " BlueOS x86_64 -- Command Prompt ";
    int tx = P_VIEW_X + (P_VIEW_W - strlen(title)) / 2;
    for (int i = 0; title[i]; i++)
        put_cell(tx + i, 1, title[i], title_color);

    draw_bar(2, BG_BLUE);
    put_cell(P_VIEW_X - 1, 2, CHAR_ML, border_color);
    draw_h_line(P_VIEW_X, 2, P_VIEW_W, CHAR_H, border_color);
    put_cell(P_VIEW_X + P_VIEW_W, 2, CHAR_MR, border_color);

    screen_set_color(FG_GREY, COLOR_BLUE);

    for (int y = P_VIEW_Y; y < P_VIEW_Y + P_VIEW_H; y++)
    {
        put_cell(P_VIEW_X - 1, y, CHAR_V, border_color);
        put_cell(P_VIEW_X + P_VIEW_W, y, CHAR_V, border_color);
    }

    draw_bar(23, BG_BLUE);
    put_cell(P_VIEW_X - 1, 23, CHAR_ML, border_color);
    draw_h_line(P_VIEW_X, 23, P_VIEW_W, CHAR_H, border_color);
    put_cell(P_VIEW_X + P_VIEW_W, 23, CHAR_MR, border_color);

    draw_bar(24, BG_BLUE);
    put_cell(P_VIEW_X - 1, 24, CHAR_BL, border_color);
    draw_h_line(P_VIEW_X, 24, P_VIEW_W, CHAR_H, border_color);
    put_cell(P_VIEW_X + P_VIEW_W, 24, CHAR_BR, border_color);

    const char* status = " Press HELP for commands ";
    int sx = P_VIEW_X + (P_VIEW_W - strlen(status)) / 2;
    for (int i = 0; status[i]; i++)
        put_cell(sx + i, 24, status[i], FG_CYAN | BG_BLUE);

    screen_set_viewport(P_VIEW_X, P_VIEW_Y, P_VIEW_W, P_VIEW_H);
    screen_clear_viewport();
}
