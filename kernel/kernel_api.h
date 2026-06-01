#ifndef KERNEL_API_H
#define KERNEL_API_H

#include "types.h"

typedef struct
{
    void (*printf)(const char* fmt, ...);
    void (*screen_putchar)(char c);
    void (*screen_write)(const char* str);
    void (*screen_set_color)(uint8_t fg, uint8_t bg);
    void (*screen_clear)(void);
    void (*screen_get_cursor)(int* x, int* y);
    void (*screen_set_cursor)(int x, int y);

    void* (*malloc)(uint32_t size);
    void (*free)(void* ptr);

    void (*memcpy)(void* dst, const void* src, uint32_t n);
    void (*memset)(void* ptr, int val, uint32_t n);
    int (*strcmp)(const char* a, const char* b);
    char* (*strcpy)(char* dst, const char* src);
    char* (*strncpy)(char* dst, const char* src, uint32_t n);
    int (*strlen)(const char* s);
    char* (*strcat)(char* dst, const char* src);
    char* (*strchr)(const char* s, int c);
    char* (*strstr)(const char* haystack, const char* needle);

    uint8_t (*inb)(uint16_t port);
    void (*outb)(uint16_t port, uint8_t val);
    uint16_t (*inw)(uint16_t port);
    void (*outw)(uint16_t port, uint16_t val);
    uint32_t (*inl)(uint16_t port);
    void (*outl)(uint16_t port, uint32_t val);

    void (*irq_install_handler)(int irq, void* handler);

    void (*register_keyb_getchar)(char (*func)(void));
    void (*register_timer_get_ticks)(uint64_t (*func)(void));
    int (*ata_read_sectors)(int io_base, int master, uint32_t lba, uint8_t count, void* buffer);
    int (*ata_write_sectors)(int io_base, int master, uint32_t lba, uint8_t count, const void* buffer);

    uint32_t fb_width;
    uint32_t fb_height;
} kernel_api_t;

#endif
