#ifndef VGA_H
#define VGA_H

#include "types.h"

#define VGA_MODE_TEXT_80x25  0

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
    uint32_t pitch;
    uint32_t* framebuffer;
    uint8_t mode;
} vga_mode_t;

int vga_init(void);
int vga_get_mode_width(void);
int vga_get_mode_height(void);
uint8_t vga_get_mode_bpp(void);

#endif
