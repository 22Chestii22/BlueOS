#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "types.h"
#include "kernel_api.h"

#define ELF_MAGIC 0x464C457F

#define ELF_ET_REL 1
#define ELF_ET_EXEC 2
#define ELF_ET_DYN 3

#define ELF_PT_LOAD 1
#define ELF_PT_DYNAMIC 2

#define ELF_DT_NULL 0
#define ELF_DT_PLTRELSZ 2
#define ELF_DT_STRTAB 5
#define ELF_DT_SYMTAB 6
#define ELF_DT_RELA 7
#define ELF_DT_RELASZ 8
#define ELF_DT_RELAENT 9
#define ELF_DT_STRSZ 10
#define ELF_DT_JMPREL 23

#define ELF_R_X86_64_NONE 0
#define ELF_R_X86_64_64 1
#define ELF_R_X86_64_PC32 2
#define ELF_R_X86_64_GLOB_DAT 6
#define ELF_R_X86_64_JUMP_SLOT 7
#define ELF_R_X86_64_RELATIVE 8
#define ELF_R_X86_64_PLT32 4

#define ELF_ST_TYPE(i) ((i) & 0xF)
#define ELF_ST_BIND(i) ((i) >> 4)
#define ELF_STT_FUNC 2
#define ELF_STB_GLOBAL 1

typedef struct
{
    uint32_t magic;
    uint8_t  bits;
    uint8_t  endian;
    uint8_t  hdr_size;
    uint8_t  osabi;
    uint8_t  padding[8];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t hdr_sz;
    uint16_t phdr_sz;
    uint16_t phnum;
    uint16_t shdr_sz;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed)) elf64_hdr_t;

typedef struct
{
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed)) elf64_phdr_t;

typedef struct
{
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t addr;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t addralign;
    uint64_t entsize;
} __attribute__((packed)) elf64_shdr_t;

typedef struct
{
    uint32_t name;
    uint8_t  info;
    uint8_t  other;
    uint16_t shndx;
    uint64_t value;
    uint64_t size;
} __attribute__((packed)) elf64_sym_t;

typedef struct
{
    uint64_t offset;
    uint64_t info;
    int64_t  addend;
} __attribute__((packed)) elf64_rela_t;

typedef struct
{
    uint64_t tag;
    uint64_t val;
} __attribute__((packed)) elf64_dyn_t;

typedef struct loaded_module
{
    char     name[32];
    void*    base_addr;
    uint64_t entry_point;
    uint64_t size;
} loaded_module_t;

int elf_load_module(const char* path, loaded_module_t* mod);
int elf_call_init(loaded_module_t* mod, kernel_api_t* api);

#endif
