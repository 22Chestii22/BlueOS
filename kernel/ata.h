#ifndef ATA_H
#define ATA_H

#include "types.h"

#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CTRL  0x376

int ata_read_sectors(int io_base, int master, uint32_t lba, uint8_t count, void* buffer);
int ata_write_sectors(int io_base, int master, uint32_t lba, uint8_t count, const void* buffer);
void ata_set_timeout(int timeout);

#endif
