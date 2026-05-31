#ifndef FAT_H
#define FAT_H

#include "types.h"

int fat_register(void);
uint32_t fat_get_root_cluster(void);
uint32_t fat_get_bytes_per_sector(void);
uint32_t fat_get_sectors_per_cluster(void);
uint32_t fat_get_data_start(void);
int fat_is_mounted(void);
const char* fat_get_volume_label(void);

#endif
