#include "types.h"
#include "io.h"
#include "serial.h"

static uint16_t* vga_buffer = (uint16_t*)VGA_MEMORY;
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t vga_color = 0x07;
static void (*redirect_callback)(char) = NULL;

enum VGA_COLOR
{
    COLOR_BLACK = 0,
    COLOR_BLUE = 1,
    COLOR_GREEN = 2,
    COLOR_CYAN = 3,
    COLOR_RED = 4,
    COLOR_MAGENTA = 5,
    COLOR_BROWN = 6,
    COLOR_LIGHT_GREY = 7,
    COLOR_DARK_GREY = 8,
    COLOR_LIGHT_BLUE = 9,
    COLOR_LIGHT_GREEN = 10,
    COLOR_LIGHT_CYAN = 11,
    COLOR_LIGHT_RED = 12,
    COLOR_LIGHT_MAGENTA = 13,
    COLOR_LIGHT_BROWN = 14,
    COLOR_WHITE = 15,
};

static uint16_t make_char(char c, uint8_t color)
{
    return (uint16_t)c | (uint16_t)color << 8;
}

void screen_set_color(uint8_t fg, uint8_t bg)
{
    vga_color = fg | (bg << 4);
}

void screen_putchar(char c)
{
    serial_write_char(c);
    if (redirect_callback)
        redirect_callback(c);
    if (c == '\n')
    {
        cursor_x = 0;
        cursor_y++;
    }
    else if (c == '\r')
    {
        cursor_x = 0;
    }
    else if (c == '\t')
    {
        cursor_x = (cursor_x + 8) & ~7;
    }
    else if (c == '\b')
    {
        if (cursor_x > 0)
        {
            cursor_x--;
            vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = make_char(' ', vga_color);
        }
    }
    else
    {
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = make_char(c, vga_color);
        cursor_x++;
        if (cursor_x >= VGA_WIDTH)
        {
            cursor_x = 0;
            cursor_y++;
        }
    }

    if (cursor_y >= VGA_HEIGHT)
    {
        for (int y = 0; y < VGA_HEIGHT - 1; y++)
            for (int x = 0; x < VGA_WIDTH; x++)
                vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        for (int x = 0; x < VGA_WIDTH; x++)
            vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_char(' ', vga_color);
        cursor_y = VGA_HEIGHT - 1;
    }

    int pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void screen_write(const char* str)
{
    for (int i = 0; str[i]; i++)
        screen_putchar(str[i]);
}

void screen_write_hex(uint64_t val)
{
    const char* hex = "0123456789ABCDEF";
    char buf[17];
    buf[16] = 0;
    for (int i = 15; i >= 0; i--)
    {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    screen_write(buf);
}

void screen_write_dec(uint64_t val)
{
    char buf[21];
    int i = 20;
    buf[20] = 0;
    if (val == 0)
    {
        screen_write("0");
        return;
    }
    while (val > 0 && i > 0)
    {
        i--;
        buf[i] = '0' + (val % 10);
        val /= 10;
    }
    screen_write(buf + i);
}

void screen_set_redirect(void (*callback)(char))
{
    redirect_callback = callback;
}

void screen_clear(void)
{
    for (int y = 0; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            vga_buffer[y * VGA_WIDTH + x] = make_char(' ', vga_color);
    cursor_x = 0;
    cursor_y = 0;
}

void screen_get_cursor(int* x, int* y)
{
    *x = cursor_x;
    *y = cursor_y;
}

void screen_set_cursor(int x, int y)
{
    if (x < 0) x = 0;
    if (x >= VGA_WIDTH) x = VGA_WIDTH - 1;
    if (y < 0) y = 0;
    if (y >= VGA_HEIGHT) y = VGA_HEIGHT - 1;
    cursor_x = x;
    cursor_y = y;
}
