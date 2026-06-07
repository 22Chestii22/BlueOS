#include "types.h"
#include "kernel_api.h"
#include "vfs.h"

#define SECTOR_SIZE 512

typedef struct
{
    uint8_t jmp[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t media_descriptor;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    char volume_label[11];
    char fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct
{
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_res;
    uint8_t crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t cluster_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t cluster_lo;
    uint32_t file_size;
} __attribute__((packed)) fat32_dir_entry_t;

#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20
#define ATTR_LFN        0x0F

typedef struct
{
    fat32_bpb_t bpb;
    uint32_t fat_start;
    uint32_t data_start;
    uint32_t total_clusters;
    uint32_t sectors_per_fat;
    int mounted;
    int device;
} fat32_t;

static kernel_api_t* api = NULL;
static fat32_t fat;

static int fat_read_sector(uint32_t lba, void* buffer)
{
    return api->ata_read_sectors(0x1F0, 1, lba, 1, buffer);
}

static int fat_write_sector(uint32_t lba, const void* buffer)
{
    return api->ata_write_sectors(0x1F0, 1, lba, 1, buffer);
}

static uint32_t fat_get_next_cluster(uint32_t cluster)
{
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat.fat_start + (fat_offset / SECTOR_SIZE);
    uint32_t entry_offset = fat_offset % SECTOR_SIZE;

    uint8_t buffer[SECTOR_SIZE];
    if (fat_read_sector(fat_sector, buffer) != 0) return 0x0FFFFFFF;

    uint32_t next = *(uint32_t*)(buffer + entry_offset) & 0x0FFFFFFF;
    return next;
}

static void fat_set_fat_entry(uint32_t cluster, uint32_t value)
{
    uint32_t fat_offset = cluster * 4;
    uint32_t entry_offset = fat_offset % SECTOR_SIZE;
    uint8_t buffer[SECTOR_SIZE];

    for (int copy = 0; copy < fat.bpb.num_fats; copy++)
    {
        uint32_t fat_sector = fat.fat_start + copy * fat.sectors_per_fat + (fat_offset / SECTOR_SIZE);
        if (fat_read_sector(fat_sector, buffer) != 0) continue;

        uint32_t entry = *(uint32_t*)(buffer + entry_offset);
        entry = (entry & 0xF0000000) | (value & 0x0FFFFFFF);
        *(uint32_t*)(buffer + entry_offset) = entry;

        fat_write_sector(fat_sector, buffer);
    }
}

static void fat_free_chain(uint32_t first_cluster)
{
    uint32_t cluster = first_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8)
    {
        uint32_t next = fat_get_next_cluster(cluster);
        fat_set_fat_entry(cluster, 0);
        cluster = next;
    }
}

static uint32_t fat_alloc_clusters(uint32_t count, uint32_t* chain)
{
    uint32_t found = 0;
    uint32_t max_cluster = fat.total_clusters + 2;

    for (uint32_t c = 2; c < max_cluster && found < count; c++)
    {
        if (fat_get_next_cluster(c) == 0)
            chain[found++] = c;
    }

    if (found < count)
    {
        for (uint32_t i = 0; i < found; i++)
            fat_set_fat_entry(chain[i], 0);
        return 0x0FFFFFFF;
    }

    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t next_val = (i < count - 1) ? chain[i + 1] : 0x0FFFFFFF;
        fat_set_fat_entry(chain[i], next_val);
    }

    return chain[0];
}

static uint32_t fat_cluster_to_lba(uint32_t cluster)
{
    return fat.data_start + (cluster - 2) * fat.bpb.sectors_per_cluster;
}

static int fat_read_cluster(uint32_t cluster, void* buffer)
{
    uint32_t lba = fat_cluster_to_lba(cluster);
    int sectors = fat.bpb.sectors_per_cluster;
    uint8_t* buf = (uint8_t*)buffer;

    for (int i = 0; i < sectors; i++)
    {
        if (fat_read_sector(lba + i, buf + i * SECTOR_SIZE) != 0)
            return -1;
    }
    return 0;
}

static int fat_write_cluster(uint32_t cluster, const void* buffer)
{
    uint32_t lba = fat_cluster_to_lba(cluster);
    int sectors = fat.bpb.sectors_per_cluster;
    const uint8_t* buf = (const uint8_t*)buffer;

    for (int i = 0; i < sectors; i++)
    {
        if (fat_write_sector(lba + i, buf + i * SECTOR_SIZE) != 0)
            return -1;
    }
    return 0;
}

static int fat_name_match(const uint8_t* fat_name, const char* name)
{
    char fat_name_str[12];
    int fi = 0;

    for (int i = 0; i < 8; i++)
    {
        if (fat_name[i] == ' ') break;
        fat_name_str[fi++] = fat_name[i] >= 'a' ? fat_name[i] - 32 : fat_name[i];
    }

    int has_ext = 0;
    for (int i = 8; i < 11; i++)
    {
        if (fat_name[i] != ' ') has_ext = 1;
    }

    if (has_ext)
    {
        fat_name_str[fi++] = '.';
        for (int i = 8; i < 11; i++)
        {
            if (fat_name[i] == ' ') break;
            fat_name_str[fi++] = fat_name[i] >= 'a' ? fat_name[i] - 32 : fat_name[i];
        }
    }
    fat_name_str[fi] = 0;

    if (name[0] == '.' && fi == 2 && fat_name_str[0] == '.')
        return 1;

    char name_up[256];
    int ni = 0;
    while (name[ni] && ni < 255)
    {
        char c = name[ni];
        name_up[ni++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
    }
    name_up[ni] = 0;

    return api->strcmp(fat_name_str, name_up) == 0;
}

static void to_fat_name(const char* name, uint8_t* fat_name)
{
    api->memset(fat_name, ' ', 11);

    int dot_pos = -1;
    int len = api->strlen(name);

    for (int i = 0; i < len; i++)
    {
        if (name[i] == '.')
        {
            dot_pos = i;
            break;
        }
    }

    if (dot_pos < 0)
    {
        for (int i = 0; i < len && i < 8; i++)
        {
            char c = name[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            fat_name[i] = c;
        }
    }
    else
    {
        for (int i = 0; i < dot_pos && i < 8; i++)
        {
            char c = name[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            fat_name[i] = c;
        }
        for (int i = 0; i < (len - dot_pos - 1) && i < 3; i++)
        {
            char c = name[dot_pos + 1 + i];
            if (c >= 'a' && c <= 'z') c -= 32;
            fat_name[8 + i] = c;
        }
    }
}

static void split_path(const char* path, char* dir, char* name)
{
    int len = api->strlen(path);
    int last_sep = -1;

    for (int i = len - 1; i >= 0; i--)
    {
        if (path[i] == '/' || path[i] == '\\')
        {
            last_sep = i;
            break;
        }
    }

    if (last_sep < 0)
    {
        dir[0] = 0;
        api->strcpy(name, path);
    }
    else
    {
        api->strncpy(dir, path, last_sep);
        dir[last_sep] = 0;
        if (dir[0] == 0) api->strcpy(dir, "\\");
        api->strcpy(name, path + last_sep + 1);
    }
}

static int fat_find_entry_in_dir(uint32_t dir_cluster, const char* name,
                                  fat32_dir_entry_t* entry,
                                  uint32_t* out_cluster, uint32_t* out_offset)
{
    uint32_t cluster_size = fat.bpb.sectors_per_cluster * SECTOR_SIZE;
    uint8_t* cluster_data = (uint8_t*)api->malloc(cluster_size);
    if (!cluster_data) return -1;

    while (dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8)
    {
        if (fat_read_cluster(dir_cluster, cluster_data) != 0)
        {
            api->free(cluster_data);
            return -1;
        }

        int entries_per_cluster = cluster_size / 32;

        for (int i = 0; i < entries_per_cluster; i++)
        {
            fat32_dir_entry_t* e = (fat32_dir_entry_t*)(cluster_data + i * 32);

            if (e->name[0] == 0) break;
            if (e->name[0] == 0xE5) continue;
            if ((e->attr & ATTR_LFN) == ATTR_LFN) continue;
            if (e->attr & ATTR_VOLUME_ID) continue;

            if (fat_name_match(e->name, name))
            {
                if (entry) api->memcpy(entry, e, sizeof(fat32_dir_entry_t));
                if (out_cluster) *out_cluster = dir_cluster;
                if (out_offset) *out_offset = i * 32;
                api->free(cluster_data);
                return 0;
            }
        }

        dir_cluster = fat_get_next_cluster(dir_cluster);
    }

    api->free(cluster_data);
    return -1;
}

static int fat_find_free_slot(uint32_t dir_cluster, uint32_t* out_cluster, uint32_t* out_offset)
{
    uint32_t cluster_size = fat.bpb.sectors_per_cluster * SECTOR_SIZE;
    uint8_t* cluster_data = (uint8_t*)api->malloc(cluster_size);
    if (!cluster_data) return -1;

    while (dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8)
    {
        if (fat_read_cluster(dir_cluster, cluster_data) != 0)
        {
            api->free(cluster_data);
            return -1;
        }

        int entries_per_cluster = cluster_size / 32;

        for (int i = 0; i < entries_per_cluster; i++)
        {
            fat32_dir_entry_t* e = (fat32_dir_entry_t*)(cluster_data + i * 32);

            if (e->name[0] == 0 || e->name[0] == 0xE5)
            {
                if (out_cluster) *out_cluster = dir_cluster;
                if (out_offset) *out_offset = i * 32;
                api->free(cluster_data);
                return 0;
            }
        }

        dir_cluster = fat_get_next_cluster(dir_cluster);
    }

    api->free(cluster_data);
    return -1;
}

static int fat_write_single_entry(uint32_t cluster, uint32_t offset, fat32_dir_entry_t* entry)
{
    uint32_t cluster_size = fat.bpb.sectors_per_cluster * SECTOR_SIZE;
    uint8_t* cluster_data = (uint8_t*)api->malloc(cluster_size);
    if (!cluster_data) return -1;

    if (fat_read_cluster(cluster, cluster_data) != 0)
    {
        api->free(cluster_data);
        return -1;
    }

    api->memcpy(cluster_data + offset, entry, sizeof(fat32_dir_entry_t));
    int ret = fat_write_cluster(cluster, cluster_data);
    api->free(cluster_data);
    return ret;
}

static int fat_find_entry(const char* path, fat32_dir_entry_t* entry)
{
    if (!path || path[0] == 0) return -1;

    char clean_path[256];
    char* segments[64];
    int num_segments = 0;

    api->strncpy(clean_path, path, 255);

    char* p = clean_path;
    while (*p == '/' || *p == '\\') p++;

    if (*p == 0)
    {
        api->memset(entry, 0, sizeof(fat32_dir_entry_t));
        entry->attr = ATTR_DIRECTORY;
        entry->cluster_lo = fat.bpb.root_cluster & 0xFFFF;
        entry->cluster_hi = (fat.bpb.root_cluster >> 16) & 0xFFFF;
        return 0;
    }

    segments[num_segments++] = p;
    while (*p)
    {
        if (*p == '/' || *p == '\\')
        {
            *p = 0;
            p++;
            if (*p) segments[num_segments++] = p;
        }
        else p++;
    }

    uint32_t current_cluster = fat.bpb.root_cluster;
    fat32_dir_entry_t current_entry;
    api->memset(&current_entry, 0, sizeof(fat32_dir_entry_t));
    current_entry.cluster_lo = current_cluster & 0xFFFF;
    current_entry.cluster_hi = (current_cluster >> 16) & 0xFFFF;

    uint32_t cluster_size = fat.bpb.sectors_per_cluster * SECTOR_SIZE;
    uint8_t* cluster_data = (uint8_t*)api->malloc(cluster_size);
    if (!cluster_data) return -1;

    for (int s = 0; s < num_segments; s++)
    {
        if (fat_read_cluster(current_cluster, cluster_data) != 0)
        {
            api->free(cluster_data);
            return -1;
        }

        int entries_per_cluster = cluster_size / 32;
        int found = 0;

        for (int i = 0; i < entries_per_cluster; i++)
        {
            fat32_dir_entry_t* e = (fat32_dir_entry_t*)(cluster_data + i * 32);

            if (e->name[0] == 0) break;
            if (e->name[0] == 0xE5) continue;
            if ((e->attr & ATTR_LFN) == ATTR_LFN) continue;
            if (e->attr & ATTR_VOLUME_ID) continue;

            if (fat_name_match(e->name, segments[s]))
            {
                api->memcpy(&current_entry, e, sizeof(fat32_dir_entry_t));
                current_cluster = (e->cluster_hi << 16) | e->cluster_lo;
                found = 1;

                if (s < num_segments - 1 && !(e->attr & ATTR_DIRECTORY))
                {
                    api->free(cluster_data);
                    return -1;
                }

                break;
            }
        }

        if (!found)
        {
            api->free(cluster_data);
            return -1;
        }
    }

    api->free(cluster_data);
    api->memcpy(entry, &current_entry, sizeof(fat32_dir_entry_t));
    return 0;
}

static int fat_mount_impl(void* fs, int device)
{
    (void)fs;
    uint8_t buffer[SECTOR_SIZE];
    if (api->ata_read_sectors(0x1F0, 1, 0, 1, buffer) != 0)
        return -1;

    api->memcpy(&fat.bpb, buffer, sizeof(fat32_bpb_t));

    if (fat.bpb.bytes_per_sector != 512) return -1;
    if (fat.bpb.fat_size_32 == 0) return -1;

    fat.fat_start = fat.bpb.reserved_sectors;
    fat.sectors_per_fat = fat.bpb.fat_size_32;
    fat.data_start = fat.fat_start + fat.bpb.num_fats * fat.sectors_per_fat;
    fat.total_clusters = fat.bpb.total_sectors_32 - fat.data_start;
    fat.total_clusters /= fat.bpb.sectors_per_cluster;
    fat.device = device;
    fat.mounted = 1;

    api->printf("[FAT32] Mounted! OEM: %.8s, Size: %d MB, Root cluster: %d\n",
           fat.bpb.oem,
           fat.bpb.total_sectors_32 / 2048,
           fat.bpb.root_cluster);

    return 0;
}

static int fat_read_impl(void* fs, const char* path, uint32_t position, void* buffer, uint32_t size)
{
    (void)fs;
    fat32_dir_entry_t entry;
    if (fat_find_entry(path, &entry) != 0) return -1;
    if (entry.attr & ATTR_DIRECTORY) return -1;

    uint32_t cluster = (entry.cluster_hi << 16) | entry.cluster_lo;
    uint32_t bytes_read = 0;
    uint32_t to_read = size < entry.file_size ? size : entry.file_size;
    uint8_t* buf = (uint8_t*)buffer;

    uint32_t cluster_size = fat.bpb.sectors_per_cluster * SECTOR_SIZE;
    uint8_t* cluster_data = (uint8_t*)api->malloc(cluster_size);
    if (!cluster_data) return -1;

    uint32_t skip = position;
    while (skip > 0 && cluster < 0x0FFFFFF8)
    {
        if (skip >= cluster_size)
        {
            skip -= cluster_size;
            cluster = fat_get_next_cluster(cluster);
        }
        else
        {
            if (fat_read_cluster(cluster, cluster_data) != 0)
            {
                api->free(cluster_data);
                return -1;
            }
            break;
        }
    }

    while (bytes_read < to_read && cluster < 0x0FFFFFF8)
    {
        if (fat_read_cluster(cluster, cluster_data) != 0)
        {
            api->free(cluster_data);
            return bytes_read;
        }

        uint32_t chunk = (to_read - bytes_read) < cluster_size ?
                          (to_read - bytes_read) : cluster_size;
        uint32_t offset = (skip > 0) ? skip : 0;

        api->memcpy(buf + bytes_read, cluster_data + offset, chunk - offset);
        bytes_read += chunk - offset;
        skip = 0;

        cluster = fat_get_next_cluster(cluster);
    }

    api->free(cluster_data);
    return bytes_read;
}

static int fat_get_size_impl(void* fs, const char* path)
{
    (void)fs;
    fat32_dir_entry_t entry;
    if (fat_find_entry(path, &entry) != 0) return -1;
    return (int)entry.file_size;
}

static int fat_write_impl(void* fs, const char* path, const void* buffer, uint32_t size)
{
    (void)fs;
    if (!path || !buffer || path[0] == 0) return -1;
    if (size == 0) return 0;

    char dir_path[256];
    char filename[256];
    split_path(path, dir_path, filename);
    if (filename[0] == 0) return -1;

    fat32_dir_entry_t entry;
    int exists = (fat_find_entry(path, &entry) == 0);
    if (exists && (entry.attr & ATTR_DIRECTORY)) return -1;

    fat32_dir_entry_t parent_entry;
    if (fat_find_entry(dir_path, &parent_entry) != 0) return -1;
    uint32_t parent_cluster = (parent_entry.cluster_hi << 16) | parent_entry.cluster_lo;

    uint32_t cluster_size = fat.bpb.sectors_per_cluster * SECTOR_SIZE;
    uint32_t needed_clusters = (size + cluster_size - 1) / cluster_size;
    if (needed_clusters == 0) needed_clusters = 1;
    if (needed_clusters > 0x100000) return -1;

    uint32_t* chain = (uint32_t*)api->malloc(needed_clusters * sizeof(uint32_t));
    if (!chain) return -1;

    uint32_t first_cluster = fat_alloc_clusters(needed_clusters, chain);
    if (first_cluster == 0x0FFFFFFF || first_cluster == 0)
    {
        api->free(chain);
        return -1;
    }

    int written = 0;
    uint32_t cl = first_cluster;
    const uint8_t* buf = (const uint8_t*)buffer;

    uint8_t* cluster_data = (uint8_t*)api->malloc(cluster_size);
    if (!cluster_data) { api->free(chain); return -1; }

    while (written < (int)size && cl >= 2 && cl < 0x0FFFFFF8)
    {
        uint32_t chunk = (size - written) < cluster_size ? (size - written) : cluster_size;
        api->memset(cluster_data, 0, cluster_size);
        api->memcpy(cluster_data, buf + written, chunk);

        if (fat_write_cluster(cl, cluster_data) != 0) break;

        written += chunk;
        cl = fat_get_next_cluster(cl);
    }

    fat32_dir_entry_t new_entry;
    api->memset(&new_entry, 0, sizeof(fat32_dir_entry_t));
    to_fat_name(filename, new_entry.name);
    new_entry.attr = ATTR_ARCHIVE;
    new_entry.cluster_lo = first_cluster & 0xFFFF;
    new_entry.cluster_hi = (first_cluster >> 16) & 0xFFFF;
    new_entry.file_size = size;

    if (exists)
    {
        uint32_t old_cluster = (entry.cluster_hi << 16) | entry.cluster_lo;
        if (old_cluster >= 2 && old_cluster < 0x0FFFFFF8)
            fat_free_chain(old_cluster);

        uint32_t ec, eo;
        if (fat_find_entry_in_dir(parent_cluster, filename, NULL, &ec, &eo) == 0)
            fat_write_single_entry(ec, eo, &new_entry);
    }
    else
    {
        uint32_t ec, eo;
        if (fat_find_free_slot(parent_cluster, &ec, &eo) == 0)
        {
            fat_write_single_entry(ec, eo, &new_entry);
        }
        else
        {
            fat_free_chain(first_cluster);
            api->free(chain);
            return -1;
        }
    }

    api->free(cluster_data);
    api->free(chain);
    return written;
}

static int fat_open_impl(void* fs, const char* path, int flags)
{
    (void)fs;
    fat32_dir_entry_t entry;
    if (fat_find_entry(path, &entry) == 0)
        return 1;
    if (flags & O_CREAT)
        return 1;
    return -1;
}

static int fat_close_impl(void* fs, int fd)
{
    (void)fs; (void)fd;
    return 0;
}

static int fat_readdir_impl(void* fs, const char* path, char* entries, int max_entries)
{
    (void)fs;
    fat32_dir_entry_t dir_entry;
    if (fat_find_entry(path, &dir_entry) != 0) return -1;
    if (!(dir_entry.attr & ATTR_DIRECTORY)) return -1;

    uint32_t cluster = (dir_entry.cluster_hi << 16) | dir_entry.cluster_lo;
    int count = 0;
    int pos = 0;

    uint32_t cluster_size = fat.bpb.sectors_per_cluster * SECTOR_SIZE;
    uint8_t* cluster_data = (uint8_t*)api->malloc(cluster_size);
    if (!cluster_data) return -1;

    while (cluster >= 2 && cluster < 0x0FFFFFF8)
    {
        if (fat_read_cluster(cluster, cluster_data) != 0) break;

        int entries_per_cluster = cluster_size / 32;

        for (int i = 0; i < entries_per_cluster; i++)
        {
            fat32_dir_entry_t* e = (fat32_dir_entry_t*)(cluster_data + i * 32);

            if (e->name[0] == 0) goto done;
            if (e->name[0] == 0xE5) continue;
            if ((e->attr & ATTR_LFN) == ATTR_LFN) continue;
            if (e->attr & ATTR_VOLUME_ID) continue;

            if (pos + 24 > max_entries) goto done;

            if (e->attr & ATTR_DIRECTORY)
            {
                entries[pos++] = 'D';
                entries[pos++] = ' ';
            }
            else
            {
                entries[pos++] = ' ';
                entries[pos++] = ' ';
            }

            for (int j = 0; j < 8; j++)
            {
                if (e->name[j] == ' ') break;
                entries[pos++] = e->name[j];
            }
            if (!(e->attr & ATTR_DIRECTORY))
            {
                entries[pos++] = '.';
                for (int j = 8; j < 11; j++)
                {
                    if (e->name[j] == ' ') break;
                    entries[pos++] = e->name[j];
                }
            }
            entries[pos++] = 0;

            if (e->attr & ATTR_DIRECTORY)
            {
                entries[pos++] = 0;
            }
            else
            {
                uint32_t sz = e->file_size;
                char szbuf[16];
                int si = 0;
                if (sz == 0) szbuf[si++] = '0';
                while (sz > 0) { szbuf[si++] = '0' + (sz % 10); sz /= 10; }
                for (int j = si - 1; j >= 0; j--)
                    entries[pos++] = szbuf[j];
                entries[pos++] = 0;
            }
            count++;
        }

        cluster = fat_get_next_cluster(cluster);
    }

done:
    api->free(cluster_data);
    entries[pos] = 0;
    return pos;
}

static int fat_mkdir_impl(void* fs, const char* path)
{
    (void)fs;
    if (!path || path[0] == 0) return -1;

    fat32_dir_entry_t existing;
    if (fat_find_entry(path, &existing) == 0) return -1;

    char dir_path[256];
    char dirname[256];
    split_path(path, dir_path, dirname);
    if (dirname[0] == 0) return -1;

    fat32_dir_entry_t parent_entry;
    if (fat_find_entry(dir_path, &parent_entry) != 0) return -1;
    uint32_t parent_cluster = (parent_entry.cluster_hi << 16) | parent_entry.cluster_lo;

    uint32_t cluster = fat_alloc_clusters(1, &cluster);
    if (cluster == 0x0FFFFFFF || cluster == 0) return -1;

    uint32_t cluster_size = fat.bpb.sectors_per_cluster * SECTOR_SIZE;
    uint8_t* cluster_data = (uint8_t*)api->malloc(cluster_size);
    if (!cluster_data)
    {
        fat_set_fat_entry(cluster, 0);
        return -1;
    }
    api->memset(cluster_data, 0, cluster_size);

    fat32_dir_entry_t dot;
    api->memset(&dot, 0, sizeof(dot));
    api->memset(dot.name, ' ', 11);
    dot.name[0] = '.';
    dot.attr = ATTR_DIRECTORY;
    dot.cluster_lo = cluster & 0xFFFF;
    dot.cluster_hi = (cluster >> 16) & 0xFFFF;
    api->memcpy(cluster_data, &dot, sizeof(fat32_dir_entry_t));

    fat32_dir_entry_t dotdot;
    api->memset(&dotdot, 0, sizeof(dotdot));
    api->memset(dotdot.name, ' ', 11);
    dotdot.name[0] = '.';
    dotdot.name[1] = '.';
    dotdot.attr = ATTR_DIRECTORY;
    dotdot.cluster_lo = parent_cluster & 0xFFFF;
    dotdot.cluster_hi = (parent_cluster >> 16) & 0xFFFF;
    api->memcpy(cluster_data + 32, &dotdot, sizeof(fat32_dir_entry_t));

    if (fat_write_cluster(cluster, cluster_data) != 0)
    {
        fat_set_fat_entry(cluster, 0);
        api->free(cluster_data);
        return -1;
    }
    api->free(cluster_data);

    uint32_t ec, eo;
    if (fat_find_free_slot(parent_cluster, &ec, &eo) != 0)
    {
        fat_set_fat_entry(cluster, 0);
        return -1;
    }

    fat32_dir_entry_t new_entry;
    api->memset(&new_entry, 0, sizeof(fat32_dir_entry_t));
    to_fat_name(dirname, new_entry.name);
    new_entry.attr = ATTR_DIRECTORY;
    new_entry.cluster_lo = cluster & 0xFFFF;
    new_entry.cluster_hi = (cluster >> 16) & 0xFFFF;

    if (fat_write_single_entry(ec, eo, &new_entry) != 0)
    {
        fat_set_fat_entry(cluster, 0);
        return -1;
    }

    return 0;
}

static int fat_unlink_impl(void* fs, const char* path)
{
    (void)fs;
    if (!path || path[0] == 0) return -1;

    fat32_dir_entry_t entry;
    if (fat_find_entry(path, &entry) != 0) return -1;

    char dir_path[256];
    char filename[256];
    split_path(path, dir_path, filename);
    if (filename[0] == 0) return -1;

    fat32_dir_entry_t parent_entry;
    if (fat_find_entry(dir_path, &parent_entry) != 0) return -1;
    uint32_t parent_cluster = (parent_entry.cluster_hi << 16) | parent_entry.cluster_lo;

    uint32_t dir_cluster, entry_offset;
    if (fat_find_entry_in_dir(parent_cluster, filename, NULL, &dir_cluster, &entry_offset) != 0)
        return -1;

    uint32_t cluster = (entry.cluster_hi << 16) | entry.cluster_lo;
    if (cluster >= 2 && cluster < 0x0FFFFFF8)
        fat_free_chain(cluster);

    uint8_t sector_data[SECTOR_SIZE];
    uint32_t entry_sector = fat_cluster_to_lba(dir_cluster) + (entry_offset / SECTOR_SIZE);
    uint32_t sector_offset = entry_offset % SECTOR_SIZE;

    if (fat_read_sector(entry_sector, sector_data) != 0) return -1;
    sector_data[sector_offset] = 0xE5;
    fat_write_sector(entry_sector, sector_data);

    return 0;
}

static int fat_rename_impl(void* fs, const char* old_path, const char* new_path)
{
    (void)fs;
    fat32_dir_entry_t entry;
    if (fat_find_entry(old_path, &entry) != 0) return -1;
    if (fat_find_entry(new_path, &entry) == 0) return -1;

    char old_dir[256], old_name[256];
    char new_dir[256], new_name[256];
    split_path(old_path, old_dir, old_name);
    split_path(new_path, new_dir, new_name);

    if (api->strcmp(old_dir, new_dir) != 0) return -1;

    fat32_dir_entry_t parent_entry;
    if (fat_find_entry(old_dir, &parent_entry) != 0) return -1;
    uint32_t parent_cluster = (parent_entry.cluster_hi << 16) | parent_entry.cluster_lo;

    uint32_t dir_cluster, entry_offset;
    if (fat_find_entry_in_dir(parent_cluster, old_name, NULL, &dir_cluster, &entry_offset) != 0)
        return -1;

    uint32_t cluster_size = fat.bpb.sectors_per_cluster * SECTOR_SIZE;
    uint8_t* cluster_data = (uint8_t*)api->malloc(cluster_size);
    if (!cluster_data) return -1;

    if (fat_read_cluster(dir_cluster, cluster_data) != 0)
    {
        api->free(cluster_data);
        return -1;
    }

    fat32_dir_entry_t* ep = (fat32_dir_entry_t*)(cluster_data + entry_offset);
    to_fat_name(new_name, ep->name);

    int ret = fat_write_cluster(dir_cluster, cluster_data);
    api->free(cluster_data);
    return ret;
}

static int fat_register(void)
{
    return vfs_mount(
        "FAT32", 0,
        fat_mount_impl,
        (void*)fat_read_impl,
        fat_write_impl,
        fat_open_impl,
        fat_close_impl,
        fat_readdir_impl,
        fat_mkdir_impl,
        fat_unlink_impl,
        fat_rename_impl,
        fat_get_size_impl
    );
}

const char* fat_get_volume_label(void)
{
    if (!fat.mounted) return "";
    return fat.bpb.volume_label;
}

uint32_t fat_get_root_cluster(void)
{
    return fat.bpb.root_cluster;
}

uint32_t fat_get_bytes_per_sector(void)
{
    return fat.bpb.bytes_per_sector;
}

uint32_t fat_get_sectors_per_cluster(void)
{
    return fat.bpb.sectors_per_cluster;
}

uint32_t fat_get_data_start(void)
{
    return fat.data_start;
}

int fat_is_mounted(void)
{
    return fat.mounted;
}

void fat_module_init(kernel_api_t* kapi)
{
    api = kapi;
    fat_register();
    api->printf("[FAT32] Module loaded\n");
}
