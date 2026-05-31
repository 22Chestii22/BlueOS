#include "types.h"
#include "vga.h"

static vga_mode_t current_mode;

int vga_init(void)
{
    current_mode.width = VGA_WIDTH * 8;
    current_mode.height = VGA_HEIGHT * 16;
    current_mode.bpp = 4;
    current_mode.pitch = VGA_WIDTH * 2;
    current_mode.framebuffer = (uint32_t*)0;
    current_mode.mode = VGA_MODE_TEXT_80x25;
    return 0;
}

int vga_get_mode_width(void)
{
    return current_mode.width;
}

int vga_get_mode_height(void)
{
    return current_mode.height;
}

uint8_t vga_get_mode_bpp(void)
{
    return current_mode.bpp;
}
