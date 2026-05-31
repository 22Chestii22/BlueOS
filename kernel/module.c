#include "types.h"
#include "string.h"
#include "screen.h"
#include "mem.h"
#include "io.h"
#include "idt.h"
#include "module.h"
#include "ata.h"

static char (*registered_keyb_getchar)(void) = NULL;
static uint64_t (*registered_timer_get_ticks)(void) = NULL;

static void reg_keyb(char (*func)(void))
{
    registered_keyb_getchar = func;
}

static void reg_timer(uint64_t (*func)(void))
{
    registered_timer_get_ticks = func;
}

kernel_api_t kernel_api =
{
    .printf = printf,
    .screen_putchar = screen_putchar,
    .screen_write = screen_write,
    .screen_set_color = screen_set_color,
    .screen_clear = screen_clear,
    .screen_get_cursor = screen_get_cursor,
    .screen_set_cursor = screen_set_cursor,
    .malloc = malloc,
    .free = free,
    .memcpy = (void (*)(void*, const void*, uint32_t))memcpy,
    .memset = (void (*)(void*, int, uint32_t))memset,
    .strcmp = strcmp,
    .strcpy = strcpy,
    .strncpy = strncpy,
    .strlen = (int (*)(const char*))strlen,
    .strcat = strcat,
    .strchr = strchr,
    .strstr = strstr,
    .inb = inb,
    .outb = outb,
    .inw = inw,
    .outw = outw,
    .inl = inl,
    .outl = outl,
    .irq_install_handler = (void (*)(int, void*))irq_install_handler,
    .register_keyb_getchar = reg_keyb,
    .register_timer_get_ticks = reg_timer,
    .ata_read_sectors = ata_read_sectors,
    .ata_write_sectors = ata_write_sectors,
};

char keyb_getchar_wrapper(void)
{
    if (registered_keyb_getchar)
        return (*registered_keyb_getchar)();
    return 0;
}

uint64_t timer_get_ticks_wrapper(void)
{
    if (registered_timer_get_ticks)
        return (*registered_timer_get_ticks)();
    return 0;
}
