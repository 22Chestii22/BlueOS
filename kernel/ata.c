#include "types.h"
#include "io.h"

#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CTRL  0x376

#define ATA_REG_DATA        0
#define ATA_REG_ERROR       1
#define ATA_REG_FEATURES    1
#define ATA_REG_SECCOUNT    2
#define ATA_REG_LBA0        3
#define ATA_REG_LBA1        4
#define ATA_REG_LBA2        5
#define ATA_REG_DRIVE       6
#define ATA_REG_STATUS      7
#define ATA_REG_COMMAND     7

#define ATA_SR_BSY          0x80
#define ATA_SR_DRDY         0x40
#define ATA_SR_DF           0x20
#define ATA_SR_DSC          0x10
#define ATA_SR_DRQ          0x08
#define ATA_SR_CORR         0x04
#define ATA_SR_IDX          0x02
#define ATA_SR_ERR          0x01

#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_CACHE_FLUSH 0xE7

#define ATA_TIMEOUT_DEFAULT 10000000

static int ata_timeout = ATA_TIMEOUT_DEFAULT;

void ata_set_timeout(int timeout)
{
    ata_timeout = timeout;
}

static int ata_wait(int io_base, int check_drq)
{
    uint8_t status;
    int timeout = ata_timeout;

    for (int i = 0; i < 4; i++)
        inb(io_base + ATA_REG_STATUS);

    while (timeout--)
    {
        status = inb(io_base + ATA_REG_STATUS);
        if (status & ATA_SR_BSY)
            continue;
        if (status & ATA_SR_ERR)
            return -1;
        if (check_drq)
        {
            if (status & ATA_SR_DRQ)
                return 0;
        }
        else
        {
            return 0;
        }
    }

    return -1;
}

int ata_read_sectors(int io_base, int master, uint32_t lba, uint8_t count, void* buffer)
{
    int drive = master ? 0xE0 : 0xF0;

    outb(io_base + ATA_REG_DRIVE, drive | ((lba >> 24) & 0x0F));
    if (ata_wait(io_base, 0))
        return -1;

    outb(io_base + ATA_REG_FEATURES, 0);
    outb(io_base + ATA_REG_SECCOUNT, count);
    outb(io_base + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(io_base + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(io_base + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(io_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    uint16_t* buf = (uint16_t*)buffer;

    for (int sector = 0; sector < count; sector++)
    {
        if (ata_wait(io_base, 1))
            return -1;

        for (int i = 0; i < 256; i++)
            buf[i] = inw(io_base + ATA_REG_DATA);

        buf += 256;
    }

    return 0;
}

int ata_write_sectors(int io_base, int master, uint32_t lba, uint8_t count, const void* buffer)
{
    int drive = master ? 0xE0 : 0xF0;

    outb(io_base + ATA_REG_DRIVE, drive | ((lba >> 24) & 0x0F));
    if (ata_wait(io_base, 0))
        return -1;

    outb(io_base + ATA_REG_FEATURES, 0);
    outb(io_base + ATA_REG_SECCOUNT, count);
    outb(io_base + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(io_base + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(io_base + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    const uint16_t* buf = (const uint16_t*)buffer;

    for (int sector = 0; sector < count; sector++)
    {
        if (ata_wait(io_base, 1))
            return -1;

        for (int i = 0; i < 256; i++)
            outw(io_base + ATA_REG_DATA, buf[i]);

        outb(io_base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        if (ata_wait(io_base, 0))
            return -1;

        buf += 256;
    }

    return 0;
}
