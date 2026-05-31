#include "types.h"
#include "string.h"
#include "mem.h"
#include "screen.h"
#include "paging.h"
#include "fb.h"
#include "font.h"

fb_info_t fb_info;

static uint32_t* backbuffer = NULL;
static uint32_t* mapped_fb = NULL;

void fb_init(uint64_t phys_addr, uint32_t width, uint32_t height, uint32_t pitch, uint8_t bpp)
{
    fb_info.addr = phys_addr;
    fb_info.width = width;
    fb_info.height = height;
    fb_info.pitch = pitch;
    fb_info.bpp = bpp;

    uint32_t fb_size = height * pitch;

    uint32_t pages = (fb_size + 0xFFF) / 0x1000;
    uint64_t vaddr = phys_addr;
    uint64_t paddr = phys_addr;

    for (uint32_t i = 0; i < pages; i++)
        map_page(vaddr + i * 0x1000, paddr + i * 0x1000, 0x03);

    mapped_fb = (uint32_t*)phys_addr;

    printf("[FB] %dx%d %dbpp pitch=%d addr=0x%x\n", width, height, bpp, pitch, (uint32_t)phys_addr);
}

void fb_backbuffer_alloc(void)
{
    if (backbuffer) return;
    uint32_t size = fb_info.height * fb_info.pitch;
    backbuffer = (uint32_t*)malloc(size);
    if (backbuffer)
        memset(backbuffer, 0, size);
}

static void putpixel_raw(uint32_t x, uint32_t y, uint32_t color)
{
    if (x >= fb_info.width || y >= fb_info.height) return;
    if (fb_info.bpp == 32)
    {
        uint32_t* ptr = (uint32_t*)((uint8_t*)mapped_fb + y * fb_info.pitch);
        ptr[x] = color;
    }
    else if (fb_info.bpp == 24)
    {
        uint8_t* ptr = (uint8_t*)mapped_fb + y * fb_info.pitch + x * 3;
        ptr[0] = color & 0xFF;
        ptr[1] = (color >> 8) & 0xFF;
        ptr[2] = (color >> 16) & 0xFF;
    }
    else if (fb_info.bpp == 16)
    {
        uint16_t c16 = ((color >> 8) & 0xF800) | ((color >> 5) & 0x07E0) | ((color >> 3) & 0x1F);
        *((uint16_t*)((uint8_t*)mapped_fb + y * fb_info.pitch + x * 2)) = c16;
    }
}

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (backbuffer)
    {
        if (x >= fb_info.width || y >= fb_info.height) return;
        backbuffer[y * (fb_info.pitch / 4) + x] = color;
    }
    else
        putpixel_raw(x, y, color);
}

uint32_t fb_getpixel(uint32_t x, uint32_t y)
{
    if (x >= fb_info.width || y >= fb_info.height) return 0;
    if (backbuffer)
        return backbuffer[y * (fb_info.pitch / 4) + x];
    if (fb_info.bpp == 32)
        return ((uint32_t*)((uint8_t*)mapped_fb + y * fb_info.pitch))[x];
    return 0;
}

void fb_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (backbuffer)
    {
        uint32_t stride = fb_info.pitch / 4;
        for (uint32_t row = 0; row < h; row++)
        {
            uint32_t yy = y + row;
            if (yy >= fb_info.height) break;
            for (uint32_t col = 0; col < w; col++)
            {
                uint32_t xx = x + col;
                if (xx >= fb_info.width) break;
                backbuffer[yy * stride + xx] = color;
            }
        }
    }
    else
    {
        for (uint32_t row = 0; row < h; row++)
            for (uint32_t col = 0; col < w; col++)
                putpixel_raw(x + col, y + row, color);
    }
}

void fb_draw_vline(int x, int y0, int y1, uint32_t color)
{
    if (x < 0 || (uint32_t)x >= fb_info.width) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y1 < 0 || y0 >= (int)fb_info.height) return;
    if (y0 < 0) y0 = 0;
    if ((uint32_t)y1 >= fb_info.height) y1 = fb_info.height - 1;

    if (backbuffer)
    {
        uint32_t stride = fb_info.pitch / 4;
        for (int row = y0; row <= y1; row++)
            backbuffer[row * stride + x] = color;
    }
    else
    {
        for (int row = y0; row <= y1; row++)
            putpixel_raw(x, row, color);
    }
}

void fb_draw_hline(int y, int x0, int x1, uint32_t color)
{
    if (y < 0 || (uint32_t)y >= fb_info.height) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x1 < 0 || x0 >= (int)fb_info.width) return;
    if (x0 < 0) x0 = 0;
    if ((uint32_t)x1 >= fb_info.width) x1 = fb_info.width - 1;

    if (backbuffer)
    {
        uint32_t stride = fb_info.pitch / 4;
        uint32_t* row = &backbuffer[y * stride];
        for (int col = x0; col <= x1; col++)
            row[col] = color;
    }
    else
    {
        for (int col = x0; col <= x1; col++)
            putpixel_raw(col, y, color);
    }
}

void fb_drawchar(int x, int y, char c, uint32_t fg, uint32_t bg)
{
    int ci = (unsigned char)c - FONT_FIRST_CHAR;
    if (ci < 0 || ci >= FONT_NUM_CHARS) return;

    for (int row = 0; row < FONT_HEIGHT; row++)
    {
        unsigned char bits = font_data[ci][row];
        int py = y + row;
        if (py < 0 || (uint32_t)py >= fb_info.height) continue;
        for (int col = 0; col < FONT_WIDTH; col++)
        {
            int px = x + col;
            if (px < 0 || (uint32_t)px >= fb_info.width) continue;
            uint32_t color = (bits & (1 << (7 - col))) ? fg : bg;
            fb_putpixel(px, py, color);
        }
    }
}

void fb_drawstring(int x, int y, const char* str, uint32_t fg, uint32_t bg)
{
    int cx = x;
    for (int i = 0; str[i]; i++)
    {
        if (str[i] == '\n')
        {
            cx = x;
            y += FONT_HEIGHT;
            continue;
        }
        fb_drawchar(cx, y, str[i], fg, bg);
        cx += FONT_WIDTH;
    }
}

void fb_blit(void)
{
    if (!backbuffer) return;
    uint32_t size = fb_info.height * fb_info.pitch;
    memcpy(mapped_fb, backbuffer, size);
}

void fb_clear(uint32_t color)
{
    fb_fillrect(0, 0, fb_info.width, fb_info.height, color);
}