#include "types.h"
#include "string.h"
#include "screen.h"
#include "mem.h"
#include "vfs.h"
#include "elf_loader.h"

int elf_load_module(const char* path, loaded_module_t* mod)
{
    int fd = vfs_open(path, 0);
    if (fd < 0) return -1;

    uint8_t* file_data = malloc(65536);
    if (!file_data) { vfs_close(fd); return -1; }
    memset(file_data, 0, 65536);

    int total = vfs_read(fd, file_data, 65536);
    vfs_close(fd);
    if (total < (int)sizeof(elf64_hdr_t)) { free(file_data); return -1; }

    elf64_hdr_t* hdr = (elf64_hdr_t*)file_data;
    if (hdr->magic != ELF_MAGIC || hdr->bits != 2 || hdr->machine != 0x3E)
    { free(file_data); return -1; }
    if (hdr->type != ELF_ET_DYN)
    { free(file_data); return -1; }

    uint64_t min_vaddr = 0xFFFFFFFFFFFFFFFF;
    uint64_t max_vaddr = 0;
    elf64_phdr_t* phdrs = (elf64_phdr_t*)(file_data + hdr->phoff);

    for (int i = 0; i < hdr->phnum; i++)
    {
        if (phdrs[i].type == ELF_PT_LOAD)
        {
            uint64_t start = phdrs[i].vaddr;
            uint64_t end = phdrs[i].vaddr + phdrs[i].memsz;
            if (start < min_vaddr) min_vaddr = start;
            if (end > max_vaddr) max_vaddr = end;
        }
    }

    if (min_vaddr == 0xFFFFFFFFFFFFFFFF)
    { free(file_data); return -1; }

    uint64_t load_size = max_vaddr - min_vaddr;
    void* base = malloc(load_size);
    if (!base) { free(file_data); return -1; }
    memset(base, 0, load_size);

    for (int i = 0; i < hdr->phnum; i++)
    {
        if (phdrs[i].type == ELF_PT_LOAD)
        {
            uint64_t offset = phdrs[i].vaddr - min_vaddr;
            if (phdrs[i].filesz > 0)
            {
                if (phdrs[i].offset + phdrs[i].filesz <= (uint64_t)total)
                    memcpy((uint8_t*)base + offset, file_data + phdrs[i].offset, phdrs[i].filesz);
            }
        }
    }

    uint64_t base_addr = (uint64_t)base - min_vaddr;

    elf64_dyn_t* dyn = NULL;
    for (int i = 0; i < hdr->phnum; i++)
    {
        if (phdrs[i].type == ELF_PT_DYNAMIC)
        {
            uint64_t offset = phdrs[i].vaddr - min_vaddr;
            if (phdrs[i].filesz > 0 && offset + phdrs[i].filesz <= load_size)
                dyn = (elf64_dyn_t*)((uint64_t)base + offset);
            break;
        }
    }

    void* rela = NULL;
    uint64_t rela_size = 0;
    uint64_t rela_entsize = sizeof(elf64_rela_t);
    void* symtab = NULL;
    void* strtab = NULL;
    uint64_t symtab_entsize = 24;

    if (dyn)
    {
        while (dyn->tag != ELF_DT_NULL)
        {
            uint64_t val = dyn->val;
            switch (dyn->tag)
            {
            case ELF_DT_RELA:
                if (val >= min_vaddr)
                    rela = (void*)(base_addr + val);
                break;
            case ELF_DT_RELASZ:
                rela_size = val;
                break;
            case ELF_DT_RELAENT:
                rela_entsize = val;
                break;
            case ELF_DT_SYMTAB:
                if (val >= min_vaddr)
                    symtab = (void*)(base_addr + val);
                break;
            case ELF_DT_STRTAB:
                if (val >= min_vaddr)
                    strtab = (void*)(base_addr + val);
                break;
            }
            dyn++;
        }
    }

    if (rela && rela_size > 0)
    {
        uint64_t num_rela = rela_size / rela_entsize;
        for (uint64_t i = 0; i < num_rela; i++)
        {
            elf64_rela_t* r = (elf64_rela_t*)((uint64_t)rela + i * rela_entsize);
            uint32_t type = r->info & 0xFFFFFFFF;
            uint32_t sym_idx = r->info >> 32;
            uint64_t patch_addr;

            if (r->offset >= min_vaddr)
                patch_addr = base_addr + r->offset;
            else
                patch_addr = (uint64_t)base + (r->offset - min_vaddr);

            switch (type)
            {
            case ELF_R_X86_64_RELATIVE:
                *(uint64_t*)patch_addr = base_addr + r->addend;
                break;

            case ELF_R_X86_64_GLOB_DAT:
            case ELF_R_X86_64_64:
                if (symtab && strtab && sym_idx > 0)
                {
                    elf64_sym_t* sym = (elf64_sym_t*)((uint64_t)symtab + sym_idx * symtab_entsize);
                    uint64_t sym_val = sym->value ? base_addr + sym->value : 0;
                    *(uint64_t*)patch_addr = sym_val + r->addend;
                }
                break;

            case ELF_R_X86_64_PC32:
            case ELF_R_X86_64_PLT32:
                {
                    int64_t value;
                    if (sym_idx > 0 && symtab)
                    {
                        elf64_sym_t* sym = (elf64_sym_t*)((uint64_t)symtab + sym_idx * symtab_entsize);
                        value = sym->value ? (int64_t)(base_addr + sym->value) : 0;
                    }
                    else
                    {
                        value = 0;
                    }
                    value += r->addend;
                    value -= (int64_t)patch_addr;
                    *(uint32_t*)patch_addr = (uint32_t)(value & 0xFFFFFFFF);
                }
                break;

            case ELF_R_X86_64_NONE:
                break;
            }
        }
    }

    uint64_t entry = 0;
    if (symtab && strtab)
    {
        uint64_t num_syms = 65536 / symtab_entsize;
        for (uint64_t i = 0; i < num_syms; i++)
        {
            elf64_sym_t* sym = (elf64_sym_t*)((uint64_t)symtab + i * symtab_entsize);
            if (sym->name == 0 || sym->shndx == 0) continue;
            const char* name = (const char*)((uint64_t)strtab + sym->name);
            if (ELF_ST_TYPE(sym->info) == ELF_STT_FUNC &&
                ELF_ST_BIND(sym->info) == ELF_STB_GLOBAL &&
                strcmp(name, "module_entry") == 0)
            {
                entry = base_addr + sym->value;
                break;
            }
        }
    }

    free(file_data);

    if (!entry)
    {
        free(base);
        return -1;
    }

    int name_len = strlen(path);
    const char* basename = path;
    for (int i = name_len - 1; i >= 0; i--)
    {
        if (path[i] == '/' || path[i] == '\\')
        {
            basename = path + i + 1;
            break;
        }
    }
    strncpy(mod->name, basename, 31);
    mod->name[31] = 0;
    mod->base_addr = base;
    mod->entry_point = entry;
    mod->size = load_size;

    return 0;
}

int elf_call_init(loaded_module_t* mod, kernel_api_t* api)
{
    if (!mod || !mod->entry_point) return -1;
    void (*init)(kernel_api_t*) = (void (*)(kernel_api_t*))mod->entry_point;
    init(api);
    return 0;
}

int elf_load_modules_from_dir(const char* dir_path)
{
    char entries[4096];
    int count = vfs_readdir(dir_path, entries, sizeof(entries));
    if (count <= 0) return 0;

    int loaded = 0;
    int pos = 0;
    for (int i = 0; i < count; i++)
    {
        char type = entries[pos++];
        pos++;
        char name[256];
        int ni = 0;
        while (entries[pos] && ni < 255)
            name[ni++] = entries[pos++];
        name[ni] = 0;
        pos++;

        while (entries[pos]) pos++;
        pos++;

        if (type == 'D') continue;

        int len = strlen(name);
        if (len < 4 || strcmp(name + len - 4, ".SYS") != 0) continue;

        char full_path[256];
        strcpy(full_path, dir_path);
        int plen = strlen(full_path);
        if (full_path[plen - 1] != '/' && full_path[plen - 1] != '\\')
            strcat(full_path, "\\");
        strcat(full_path, name);

        loaded_module_t mod;
        if (elf_load_module(full_path, &mod) == 0)
        {
            printf("[LOADER] Loaded '%s' at 0x%x (entry=0x%x, size=%d)\n",
                   mod.name, (uint64_t)mod.base_addr,
                   (uint64_t)mod.entry_point, mod.size);
            if (elf_call_init(&mod, NULL) == 0)
                printf("[LOADER] Initialized '%s'\n", mod.name);
            loaded++;
        }
        else
        {
            printf("[LOADER] Failed to load '%s'\n", full_path);
        }
    }

    return loaded;
}
