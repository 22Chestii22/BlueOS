#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include "types.h"

#define BOOT_INFO_MAGIC    0x424C55454F534249ULL
#define BOOT_INFO_MAX_MMAP 32

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed)) boot_mmap_entry_t;

typedef struct {
    uint64_t magic;
    uint64_t fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint8_t  fb_bpp;
    uint8_t  reserved1[3];
    uint64_t mem_size;
    uint32_t mmap_count;
    uint32_t reserved2;
} __attribute__((packed)) boot_info_t;

#endif
