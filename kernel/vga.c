#include "types.h"
#include "vga.h"
#include "fb.h"

static vga_mode_t current_mode;

int vga_init(void)
{
    if (fb_info.width > 0 && fb_info.height > 0)
    {
        current_mode.width = fb_info.width;
        current_mode.height = fb_info.height;
        current_mode.bpp = fb_info.bpp;
        current_mode.pitch = fb_info.pitch;
        current_mode.framebuffer = (uint32_t*)fb_info.addr;
        current_mode.mode = VGA_MODE_FRAMEBUFFER;
    }
    else
    {
        current_mode.width = 1280;
        current_mode.height = 720;
        current_mode.bpp = 32;
        current_mode.pitch = 1280 * 4;
        current_mode.framebuffer = 0;
        current_mode.mode = VGA_MODE_FRAMEBUFFER;
    }
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