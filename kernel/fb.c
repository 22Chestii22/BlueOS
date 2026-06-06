#include "types.h"
#include "string.h"
#include "mem.h"
#include "screen.h"
#include "paging.h"
#include "fb.h"
#include "font.h"
#include "timer.h"
#include "process.h"

#define NUM_BACKBUFFERS 2

fb_info_t fb_info;

static uint32_t* backbuffers[NUM_BACKBUFFERS] = {NULL};
uint32_t* backbuffer = NULL;
static uint32_t* mapped_fb = NULL;
static int current_buffer = 0;

#define BACKBUFFER_VADDR_BASE 0x2000000

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
        map_page(vaddr + i * 0x1000, paddr + i * 0x1000, 0x1B);

    mapped_fb = (uint32_t*)phys_addr;

    printf("[FB] %dx%d %dbpp pitch=%d addr=0x%x\n", width, height, bpp, pitch, (uint32_t)phys_addr);
}

static uint32_t* backbuffer_alloc_pages(uint32_t size, int idx)
{
    uint32_t pages = (size + 0xFFF) / 0x1000;
    static uint64_t next_vaddr = BACKBUFFER_VADDR_BASE;
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
    UNUSED(idx);
    return (uint32_t*)vaddr;
}

void fb_backbuffer_alloc(void)
{
    if (backbuffers[0]) return;
    uint32_t size = fb_info.height * fb_info.pitch;

    for (int i = 0; i < NUM_BACKBUFFERS; i++)
    {
        backbuffers[i] = (uint32_t*)malloc(size);
        if (backbuffers[i])
        {
            memset(backbuffers[i], 0, size);
        }
        else
        {
            backbuffers[i] = backbuffer_alloc_pages(size, i);
            if (backbuffers[i])
            {
                printf("[FB] Backbuffer %d: page-allocated (%d KB)\n", i, size / 1024);
                memset(backbuffers[i], 0, size);
            }
            else
                printf("[FB] FAILED to allocate backbuffer %d (%d KB)\n", i, size / 1024);
        }
    }
    backbuffer = backbuffers[0];
    current_buffer = 0;
    printf("[FB] Backbuffers: %d buffers, %d KB each\n",
           NUM_BACKBUFFERS, size / 1024);
}

void fb_apply_desktop_bg(void)
{
    uint32_t w = fb_info.width;
    uint32_t h = fb_info.height;

    uint32_t sky_top    = FB_RGB(0x3A, 0x6E, 0xA5);
    uint32_t sky_bot    = FB_RGB(0xB8, 0xD4, 0xF0);
    uint32_t hill_far   = FB_RGB(0x8B, 0xC4, 0x6A);
    uint32_t hill_mid   = FB_RGB(0x6A, 0xB0, 0x4A);
    uint32_t hill_near  = FB_RGB(0x4A, 0x8C, 0x3F);

    uint32_t horizon = h * 2 / 3;

    for (uint32_t y = 0; y < horizon; y++)
    {
        uint8_t r = ((sky_top >> 16) & 0xFF) +
            ((((sky_bot >> 16) & 0xFF) - ((sky_top >> 16) & 0xFF)) * y / horizon);
        uint8_t g = ((sky_top >> 8) & 0xFF) +
            ((((sky_bot >> 8) & 0xFF) - ((sky_top >> 8) & 0xFF)) * y / horizon);
        uint8_t b = (sky_top & 0xFF) +
            (((sky_bot & 0xFF) - (sky_top & 0xFF)) * y / horizon);
        fb_draw_hline(y, 0, w - 1, FB_RGB(r, g, b));
    }

    uint32_t hill_h = h - horizon;

    for (uint32_t y = horizon; y < h; y++)
    {
        int dh = y - horizon;
        uint32_t col;
        if (dh < hill_h / 4)
            col = hill_far;
        else if (dh < hill_h / 2)
            col = hill_mid;
        else
            col = hill_near;
        fb_draw_hline(y, 0, w - 1, col);
    }
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
    if (x >= fb_info.width || y >= fb_info.height || w == 0 || h == 0) return;
    if (x + w > fb_info.width) w = fb_info.width - x;
    if (y + h > fb_info.height) h = fb_info.height - y;

    if (backbuffer)
    {
        uint32_t stride = fb_info.pitch / 4;
        for (uint32_t row = 0; row < h; row++)
        {
            uint32_t yy = y + row;
            uint32_t* line = &backbuffer[yy * stride + x];
            for (uint32_t col = 0; col < w; col++)
                line[col] = color;
        }
    }
    else
    {
        for (uint32_t row = 0; row < h; row++)
        {
            uint32_t yy = y + row;
            for (uint32_t col = 0; col < w; col++)
                putpixel_raw(x + col, yy, color);
        }
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

static void fb_swap_buffers(void)
{
    int next = (current_buffer + 1) % NUM_BACKBUFFERS;
    current_buffer = next;
    backbuffer = backbuffers[current_buffer];
}

static uint64_t last_blit_tick = 0;

void fb_blit(void)
{
    if (!backbuffer) return;

    uint64_t now = timer_get_ticks();
    if (now == last_blit_tick) return;
    last_blit_tick = now;

    memcpy((void*)(unsigned long)fb_info.addr, backbuffer, fb_info.height * fb_info.pitch);

    fb_swap_buffers();
}

void fb_clear(uint32_t color)
{
    fb_fillrect(0, 0, fb_info.width, fb_info.height, color);
}

uint32_t fb_blend(uint32_t fg, uint32_t bg, uint8_t alpha)
{
    uint32_t r = ((FB_GET_R(fg) * alpha) + (FB_GET_R(bg) * (255 - alpha))) / 255;
    uint32_t g = ((FB_GET_G(fg) * alpha) + (FB_GET_G(bg) * (255 - alpha))) / 255;
    uint32_t b = ((FB_GET_B(fg) * alpha) + (FB_GET_B(bg) * (255 - alpha))) / 255;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return FB_RGB(r, g, b);
}

void fb_fillrect_alpha(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint8_t alpha)
{
    if (x >= fb_info.width || y >= fb_info.height || w == 0 || h == 0) return;
    if (x + w > fb_info.width) w = fb_info.width - x;
    if (y + h > fb_info.height) h = fb_info.height - y;

    if (alpha >= 255)
    {
        fb_fillrect(x, y, w, h, color);
        return;
    }
    if (alpha == 0) return;

    if (backbuffer)
    {
        uint32_t stride = fb_info.pitch / 4;
        for (uint32_t row = 0; row < h; row++)
        {
            uint32_t yy = y + row;
            uint32_t* line = &backbuffer[yy * stride + x];
            for (uint32_t col = 0; col < w; col++)
                line[col] = fb_blend(color, line[col], alpha);
        }
    }
    else
    {
        for (uint32_t row = 0; row < h; row++)
        {
            uint32_t yy = y + row;
            for (uint32_t col = 0; col < w; col++)
            {
                uint32_t bg = 0;
                if (fb_info.bpp == 32)
                    bg = ((uint32_t*)((uint8_t*)mapped_fb + yy * fb_info.pitch))[x + col];
                putpixel_raw(x + col, yy, fb_blend(color, bg, alpha));
            }
        }
    }
}

static void fb_putchar_raw(int x, int y, char c, uint32_t fg, uint32_t bg)
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
            putpixel_raw(px, py, (bits & (1 << (7 - col))) ? fg : bg);
        }
    }
}

static void fb_puts_raw(int x, int y, const char* s, uint32_t fg, uint32_t bg)
{
    for (int i = 0; s[i]; i++)
    {
        if (s[i] == '\n') { x = 0; y += FONT_HEIGHT; continue; }
        fb_putchar_raw(x, y, s[i], fg, bg);
        x += FONT_WIDTH;
    }
}

void fb_save_region(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t* buf)
{
    if (!backbuffer || !buf) return;
    uint32_t stride = fb_info.pitch / 4;
    for (uint32_t row = 0; row < h; row++)
        for (uint32_t col = 0; col < w; col++)
            buf[row * w + col] = backbuffer[(y + row) * stride + x + col];
}

void fb_restore_region(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t* buf)
{
    if (!backbuffer || !buf) return;
    uint32_t stride = fb_info.pitch / 4;
    for (uint32_t row = 0; row < h; row++)
        for (uint32_t col = 0; col < w; col++)
            backbuffer[(y + row) * stride + x + col] = buf[row * w + col];
}

uint32_t* fb_get_backbuffer(void)
{
    return backbuffer;
}

void fb_bsod_panic(uint64_t num, uint64_t error_code, uint64_t rip)
{
    if (!mapped_fb || fb_info.width == 0 || fb_info.height == 0) return;

    uint32_t bg = FB_RGB(0, 0, 170);
    uint32_t fg = FB_RGB(255, 255, 255);

    uint32_t count = fb_info.height * (fb_info.pitch / 4);
    for (uint32_t i = 0; i < count; i++)
        mapped_fb[i] = bg;

    static const char* exc_names[] = {
        "Divide-by-zero", "Debug", "NMI", "Breakpoint", "Overflow",
        "Bound Range", "Invalid Opcode", "Device Not Available",
        "Double Fault", "Coprocessor Segment", "Invalid TSS",
        "Segment Not Present", "Stack-Segment Fault", "General Protection",
        "Page Fault", "Reserved", "x87 FPU", "Alignment Check",
        "Machine Check", "SIMD FPU", "Virtualization", "Control Protection"
    };

    int cx = 20, cy = 40;

    fb_puts_raw(cx, cy, ":(", fg, bg);
    cy += FONT_HEIGHT * 2;
    fb_puts_raw(cx, cy, "Your PC ran into a problem and needs to restart.", fg, bg);
    cy += FONT_HEIGHT + 4;
    fb_puts_raw(cx, cy, "We'll restart for you after collecting some error info.", fg, bg);
    cy += FONT_HEIGHT * 2;

    {
        char buf[64];
        int bi = 0;
        const char* p = "*** STOP: 0x000000";
        for (; *p; p++) buf[bi++] = *p;
        int hi = num >> 4;
        buf[bi++] = hi < 10 ? '0' + hi : 'A' + hi - 10;
        buf[bi++] = (num & 0xF) < 10 ? '0' + (num & 0xF) : 'A' + (num & 0xF) - 10;
        if (num < 22)
        {
            buf[bi++] = ' ';
            buf[bi++] = '(';
            for (const char* p2 = exc_names[num]; *p2; p2++) buf[bi++] = *p2;
            buf[bi++] = ')';
        }
        buf[bi] = 0;
        fb_puts_raw(cx, cy, buf, fg, bg);
    }

    cy += FONT_HEIGHT + 4;
    {
        char buf[64];
        int bi = 0;
        const char* p = "***       Error=0x";
        for (; *p; p++) buf[bi++] = *p;
        for (int s = 28; s >= 0; s -= 4)
        {
            int d = (error_code >> s) & 0xF;
            buf[bi++] = d < 10 ? '0' + d : 'A' + d - 10;
        }
        buf[bi] = 0;
        fb_puts_raw(cx, cy, buf, fg, bg);
    }

    cy += FONT_HEIGHT + 4;
    {
        char buf[64];
        int bi = 0;
        const char* p = "***       RIP=0x";
        for (; *p; p++) buf[bi++] = *p;
        for (int s = 60; s >= 0; s -= 4)
        {
            int d = (rip >> s) & 0xF;
            buf[bi++] = d < 10 ? '0' + d : 'A' + d - 10;
        }
        buf[bi] = 0;
        fb_puts_raw(cx, cy, buf, fg, bg);
    }
}
