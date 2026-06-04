#include "types.h"
#include "string.h"
#include "screen.h"
#include "mem.h"
#include "vfs.h"
#include "pe.h"
#include "process.h"
#include "gdt.h"
#include "paging.h"

typedef struct
{
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;
} __attribute__((packed)) dos_header_t;

typedef struct
{
    uint32_t signature;
    uint16_t machine;
    uint16_t number_of_sections;
    uint32_t time_date_stamp;
    uint32_t pointer_to_symbol_table;
    uint32_t number_of_symbols;
    uint16_t size_of_optional_header;
    uint16_t characteristics;
} __attribute__((packed)) coff_header_t;

#define PE_MACHINE_AMD64 0x8664

typedef struct
{
    uint16_t magic;
    uint8_t major_linker_version;
    uint8_t minor_linker_version;
    uint32_t size_of_code;
    uint32_t size_of_initialized_data;
    uint32_t size_of_uninitialized_data;
    uint32_t address_of_entry_point;
    uint32_t base_of_code;
    uint64_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint16_t major_os_version;
    uint16_t minor_os_version;
    uint16_t major_image_version;
    uint16_t minor_image_version;
    uint16_t major_subsystem_version;
    uint16_t minor_subsystem_version;
    uint32_t win32_version_value;
    uint32_t size_of_image;
    uint32_t size_of_headers;
    uint32_t check_sum;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint64_t size_of_stack_reserve;
    uint64_t size_of_stack_commit;
    uint64_t size_of_heap_reserve;
    uint64_t size_of_heap_commit;
    uint32_t loader_flags;
    uint32_t number_of_rva_and_sizes;
} __attribute__((packed)) optional_header64_t;

#define PE_MAGIC_PE32P 0x020B

typedef struct
{
    uint8_t name[8];
    union {
        uint32_t physical_address;
        uint32_t virtual_size;
    } misc;
    uint32_t virtual_address;
    uint32_t size_of_raw_data;
    uint32_t pointer_to_raw_data;
    uint32_t pointer_to_relocations;
    uint32_t pointer_to_linenumbers;
    uint16_t number_of_relocations;
    uint16_t number_of_linenumbers;
    uint32_t characteristics;
} __attribute__((packed)) section_header_t;

typedef struct
{
    uint32_t virtual_address;
    uint32_t size;
    uint16_t type;
} __attribute__((packed)) reloc_block_t;

typedef struct
{
    uint32_t import_lookup_table;
    uint32_t time_date_stamp;
    uint32_t forwarder_chain;
    uint32_t name_rva;
    uint32_t import_address_table;
} __attribute__((packed)) import_descriptor_t;

typedef struct
{
    uint64_t address;
    uint64_t size;
    char name[256];
} pe_export_t;

extern int syscall_exec(uint64_t entry, const char* name);
extern void switch_to_user_mode(uint64_t entry, uint64_t user_stack);

int load_pe_image(const char* path, uint64_t* entry_point, uint64_t* image_base)
{
    uint32_t file_size = 0;
    void* file_data = NULL;

    int fd = vfs_open(path, 0);
    if (fd < 0) return -1;

    file_size = 0;
    file_data = malloc(65536);
    if (!file_data)
    {
        vfs_close(fd);
        return -1;
    }

    int bytes_read = vfs_read(fd, file_data, 65536);
    vfs_close(fd);

    if (bytes_read <= 0)
    {
        free(file_data);
        return -1;
    }
    file_size = bytes_read;

    dos_header_t* dos = (dos_header_t*)file_data;
    if (dos->e_magic != 0x5A4D)
    {
        free(file_data);
        return -1;
    }

    if (dos->e_lfanew + sizeof(coff_header_t) + 4 > file_size)
    {
        free(file_data);
        return -1;
    }

    coff_header_t* coff = (coff_header_t*)((uint64_t)file_data + dos->e_lfanew);
    if (coff->signature != 0x00004550)
    {
        free(file_data);
        return -1;
    }

    if (coff->machine != PE_MACHINE_AMD64)
    {
        printf("[PE] Error: Only x86_64 PE32+ supported (machine=0x%x)\n", coff->machine);
        free(file_data);
        return -1;
    }

    uint64_t opt_off = (uint64_t)coff + sizeof(coff_header_t);
    if (opt_off + sizeof(optional_header64_t) > (uint64_t)file_data + file_size)
    {
        free(file_data);
        return -1;
    }

    optional_header64_t* opt = (optional_header64_t*)opt_off;
    if (opt->magic != PE_MAGIC_PE32P)
    {
        printf("[PE] Error: Only PE32+ format supported\n");
        free(file_data);
        return -1;
    }

    uint64_t base = opt->image_base;
    uint32_t image_size = opt->size_of_image;

    void* image = malloc(image_size);
    if (!image)
    {
        free(file_data);
        return -1;
    }
    memset(image, 0, image_size);

    uint32_t hdr_size = opt->size_of_headers;
    if (hdr_size > file_size) hdr_size = file_size;
    if (hdr_size > image_size) hdr_size = image_size;
    memcpy(image, file_data, hdr_size);

    uint64_t sec_off = (uint64_t)opt + coff->size_of_optional_header;
    if (sec_off + coff->number_of_sections * sizeof(section_header_t) > (uint64_t)file_data + file_size)
    {
        free(file_data);
        free(image);
        return -1;
    }
    section_header_t* sections = (section_header_t*)sec_off;

    for (int i = 0; i < coff->number_of_sections; i++)
    {
        if (sections[i].pointer_to_raw_data && sections[i].size_of_raw_data)
        {
            uint64_t dest = (uint64_t)image + sections[i].virtual_address;
            uint64_t src = (uint64_t)file_data + sections[i].pointer_to_raw_data;
            uint32_t copy_size = sections[i].size_of_raw_data;

            if (dest + copy_size > (uint64_t)image + image_size)
                copy_size = (uint64_t)image + image_size - dest;
            if (src + copy_size > (uint64_t)file_data + file_size)
                copy_size = (uint64_t)file_data + file_size - src;

            if (copy_size > 0)
                memcpy((void*)dest, (void*)src, copy_size);
        }
    }

    section_header_t* reloc_sec = NULL;
    for (int i = 0; i < coff->number_of_sections; i++)
    {
        if (sections[i].characteristics & 0x10000000)
        {
            reloc_sec = &sections[i];
            break;
        }
    }

    if (reloc_sec)
    {
        uint64_t reloc_addr = (uint64_t)image + reloc_sec->virtual_address;
        uint64_t reloc_end = reloc_addr + reloc_sec->size_of_raw_data;
        uint64_t delta = (uint64_t)image - base;

        if (reloc_end > (uint64_t)image + image_size)
            reloc_end = (uint64_t)image + image_size;

        if (delta != 0)
        {
            while (reloc_addr + sizeof(reloc_block_t) <= reloc_end)
            {
                reloc_block_t* block = (reloc_block_t*)reloc_addr;
                if (block->size < 8) break;
                if (reloc_addr + block->size > reloc_end) break;

                int num_entries = (block->size - 8) / 2;
                for (int i = 0; i < num_entries; i++)
                {
                    uint16_t* entry = (uint16_t*)(reloc_addr + 8 + i * 2);
                    int type = (*entry >> 12) & 0xF;
                    int offset = *entry & 0xFFF;

                    if (type == 0) continue;
                    if (type == 0x0A)
                    {
                        uint64_t patch_addr = (uint64_t)image + block->virtual_address + offset;
                        if (patch_addr + sizeof(uint64_t) <= (uint64_t)image + image_size)
                        {
                            uint64_t* patch = (uint64_t*)patch_addr;
                            *patch += delta;
                        }
                    }
                }

                reloc_addr += block->size;
            }
        }
    }

    import_descriptor_t* imports = NULL;

    if (opt->number_of_rva_and_sizes > 1)
    {
        uint32_t* data_dir = (uint32_t*)((uint64_t)opt + sizeof(optional_header64_t));
        if ((uint64_t)data_dir + 4 * sizeof(uint32_t) <= (uint64_t)image + image_size)
        {
            uint32_t import_dir_rva = data_dir[2];

            if (import_dir_rva)
            {
                imports = (import_descriptor_t*)((uint64_t)image + import_dir_rva);
                while (imports->name_rva)
                {
                    imports++;
                }
            }
        }
    }

    *entry_point = (uint64_t)image + opt->address_of_entry_point;
    *image_base = (uint64_t)image;

    free(file_data);
    return 0;
}

int pe_load_and_exec(const char* path, const char* args)
{
    (void)args;
    uint64_t entry_point = 0;
    uint64_t image_base = 0;

    if (load_pe_image(path, &entry_point, &image_base) != 0)
        return -1;

    uint32_t pid = syscall_exec(entry_point, path);
    if (pid == 0) return -1;

    process_t* proc = process_get_by_pid(pid);
    if (proc && proc->context)
    {
        process_set_current(proc);
        proc->state = PROCESS_RUNNING;

        gdt_set_kernel_stack((uint64_t)proc->kernel_stack + proc->kernel_stack_size);

        paging_switch(proc->page_table);

        switch_to_user_mode(proc->context->rip, proc->context->rsp);
    }

    return 0;
}

int pe_spawn(const char* path)
{
    int fd = vfs_open(path, 0);
    if (fd < 0) return -1;

    void* file_data = malloc(65536);
    if (!file_data) { vfs_close(fd); return -1; }
    int file_size = vfs_read(fd, file_data, 65536);
    vfs_close(fd);
    if (file_size <= 0) { free(file_data); return -1; }

    dos_header_t* dos = (dos_header_t*)file_data;
    if (dos->e_magic != 0x5A4D) { free(file_data); return -1; }
    if (dos->e_lfanew + sizeof(coff_header_t) + 4 > (uint32_t)file_size) { free(file_data); return -1; }

    coff_header_t* coff = (coff_header_t*)((uint64_t)file_data + dos->e_lfanew);
    if (coff->signature != 0x00004550) { free(file_data); return -1; }
    if (coff->machine != PE_MACHINE_AMD64) { free(file_data); return -1; }

    uint64_t opt_off = (uint64_t)coff + sizeof(coff_header_t);
    if (opt_off + sizeof(optional_header64_t) > (uint64_t)file_data + file_size) { free(file_data); return -1; }
    optional_header64_t* opt = (optional_header64_t*)opt_off;
    if (opt->magic != PE_MAGIC_PE32P) { free(file_data); return -1; }

    uint64_t image_base = opt->image_base;
    uint64_t entry_rva = opt->address_of_entry_point;
    uint32_t image_size = opt->size_of_image;

    void* image = malloc(image_size);
    if (!image) { free(file_data); return -1; }
    memset(image, 0, image_size);

    uint32_t hdr_size = opt->size_of_headers;
    if (hdr_size > (uint32_t)file_size) hdr_size = file_size;
    if (hdr_size > image_size) hdr_size = image_size;
    memcpy(image, file_data, hdr_size);

    uint64_t sec_off = (uint64_t)opt + coff->size_of_optional_header;
    if (sec_off + coff->number_of_sections * sizeof(section_header_t) > (uint64_t)file_data + file_size)
    {
        free(file_data); free(image); return -1;
    }
    section_header_t* sections = (section_header_t*)sec_off;

    for (int i = 0; i < coff->number_of_sections; i++)
    {
        if (sections[i].pointer_to_raw_data && sections[i].size_of_raw_data)
        {
            uint64_t dest = (uint64_t)image + sections[i].virtual_address;
            uint64_t src = (uint64_t)file_data + sections[i].pointer_to_raw_data;
            uint32_t copy_size = sections[i].size_of_raw_data;
            if (dest + copy_size > (uint64_t)image + image_size)
                copy_size = (uint64_t)image + image_size - dest;
            if (src + copy_size > (uint64_t)file_data + file_size)
                copy_size = (uint64_t)file_data + file_size - src;
            if (copy_size > 0)
                memcpy((void*)dest, (void*)src, copy_size);
        }
    }

    free(file_data);

    uint64_t virt_entry = image_base + entry_rva;
    uint32_t pid = syscall_exec(virt_entry, path);

    process_t* proc = process_get_by_pid(pid);
    if (!proc || !proc->page_table)
    {
        free(image);
        return pid > 0 ? (int)pid : -1;
    }

    uint64_t pml4 = proc->page_table;
    uint64_t image_end = image_base + image_size;

    for (uint64_t va = image_base & ~0xFFF; va < image_end; va += 0x1000)
    {
        uint64_t phys = paging_alloc_frame();
        memset((void*)phys, 0, 0x1000);
        uint64_t buf_off = va - image_base;
        uint32_t copy = 0x1000;
        if (buf_off + copy > image_size)
            copy = image_size - buf_off;
        if (copy > 0)
            memcpy((void*)phys, (uint8_t*)image + buf_off, copy);
        paging_map_user(pml4, va, phys, 0x007);
    }

    free(image);
    return (int)pid;
}

int pe_check_format(const char* path)
{
    uint8_t header[512];

    int fd = vfs_open(path, 0);
    if (fd < 0) return 0;

    int bytes = vfs_read(fd, header, 512);
    vfs_close(fd);

    if (bytes < 64) return 0;

    dos_header_t* dos = (dos_header_t*)header;
    if (dos->e_magic != 0x5A4D) return 0;
    if (dos->e_lfanew + 4 > (uint32_t)bytes) return 0;

    uint32_t* pe_sig = (uint32_t*)(header + dos->e_lfanew);
    return *pe_sig == 0x00004550;
}
