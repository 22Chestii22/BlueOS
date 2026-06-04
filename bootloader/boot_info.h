#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stdint.h>

#define BOOT_INFO_MAGIC    0x424C55454F534249ULL  /* "BLUEOSBI" */
#define BOOT_INFO_MAX_MMAP 32

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed)) boot_mmap_entry_t;

typedef struct {
    uint64_t magic;           /* BOOT_INFO_MAGIC */
    uint64_t fb_addr;         /* framebuffer physical address */
    uint32_t fb_width;        /* framebuffer width in pixels */
    uint32_t fb_height;       /* framebuffer height in pixels */
    uint32_t fb_pitch;        /* framebuffer pitch in bytes */
    uint8_t  fb_bpp;          /* framebuffer bits per pixel */
    uint8_t  reserved1[3];
    uint64_t mem_size;        /* total usable memory in bytes */
    uint32_t mmap_count;      /* number of memory map entries */
    uint32_t reserved2;
    /* boot_mmap_entry_t mmap_entries[mmap_count] follows */
} __attribute__((packed)) boot_info_t;

#endif
