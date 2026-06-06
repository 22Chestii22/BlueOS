#include "types.h"
#include "string.h"
#include "screen.h"
#include "mem.h"
#include "vfs.h"
#include "blu.h"
#include "process.h"
#include "paging.h"

typedef struct
{
    char     magic[4];
    uint32_t entry_rva;
    uint32_t image_base;
    uint64_t image_size;
    uint64_t bss_size;
    char     name[32];
} __attribute__((packed)) blu_header_t;

extern int syscall_exec(uint64_t entry, const char* name);

int blu_spawn(const char* path)
{
    int fd = vfs_open(path, 0);
    if (fd < 0) return -1;

    void* file_data = malloc(65536);
    if (!file_data) { vfs_close(fd); return -1; }

    int file_size = vfs_read(fd, file_data, 65536);
    vfs_close(fd);
    if (file_size < (int)sizeof(blu_header_t)) { free(file_data); return -1; }

    blu_header_t* hdr = (blu_header_t*)file_data;
    if (hdr->magic[0] != 'B' || hdr->magic[1] != 'L' ||
        hdr->magic[2] != 'U' || hdr->magic[3] != 0x01)
    {
        free(file_data);
        return -1;
    }

    uint64_t base = hdr->image_base;
    uint64_t image_size = hdr->image_size;
    uint64_t bss_size = hdr->bss_size;
    uint64_t entry = base + hdr->entry_rva;
    uint64_t image_pages = (image_size + 0xFFF) >> 12;
    uint64_t code_size = image_size - bss_size;
    uint32_t code_in_file = (uint32_t)file_size - sizeof(blu_header_t);
    if (code_in_file > code_size)
        code_in_file = (uint32_t)code_size;

    uint8_t* image = malloc(image_size);
    if (!image) { free(file_data); return -1; }
    memset(image, 0, image_size);

    if (code_in_file > 0)
        memcpy(image, (uint8_t*)file_data + sizeof(blu_header_t), code_in_file);

    free(file_data);

    uint32_t pid = syscall_exec(entry, path);
    if (pid == 0) { free(image); return -1; }

    process_t* proc = process_get_by_pid(pid);
    if (!proc || !proc->page_table) { free(image); return pid > 0 ? (int)pid : -1; }

    uint64_t pml4 = proc->page_table;

    for (uint64_t i = 0; i < image_pages; i++)
    {
        uint64_t va = base + i * 0x1000;
        uint64_t phys = paging_alloc_frame();
        memset((void*)phys, 0, 0x1000);
        uint64_t buf_off = i * 0x1000;
        uint32_t copy = 0x1000;
        if (buf_off + copy > image_size)
            copy = image_size - buf_off;
        if (copy > 0)
            memcpy((void*)phys, image + buf_off, copy);
        paging_map_user(pml4, va, phys, 0x007);
    }

    free(image);
    return (int)pid;
}

int blu_check_format(const char* path)
{
    uint8_t header[64];

    int fd = vfs_open(path, 0);
    if (fd < 0) return 0;

    int bytes = vfs_read(fd, header, 64);
    vfs_close(fd);

    if (bytes < 60) return 0;

    blu_header_t* hdr = (blu_header_t*)header;
    return (hdr->magic[0] == 'B' && hdr->magic[1] == 'L' &&
            hdr->magic[2] == 'U' && hdr->magic[3] == 0x01) ? 1 : 0;
}
