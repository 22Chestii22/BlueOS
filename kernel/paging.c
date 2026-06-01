#include "types.h"
#include "string.h"
#include "screen.h"
#include "mem.h"

typedef struct page
{
    uint32_t present    : 1;
    uint32_t rw         : 1;
    uint32_t user       : 1;
    uint32_t accessed   : 1;
    uint32_t dirty      : 1;
    uint32_t unused     : 7;
    uint32_t frame      : 20;
} page_t;

typedef struct page_table
{
    page_t pages[1024];
} page_table_t;

typedef struct page_directory
{
    page_table_t* tables[1024];
    uint64_t tables_physical[1024];
    uint64_t physical_addr;
} page_directory_t;

static page_directory_t* kernel_dir = NULL;
static uint32_t next_free_frame = 0;
static uint32_t max_frames = 0;
uint64_t kernel_cr3 = 0;

#define FRAME_SIZE 0x1000
#define PAGE_TABLE_ENTRIES 512
#define PAGE_DIRECTORY_ENTRIES 512
#define PAGE_2MB 0x200000

static uint32_t* frame_bitmap = NULL;
static uint32_t total_frames = 0;

static uint32_t alloc_frame(void)
{
    for (uint32_t i = next_free_frame; i < total_frames; i++)
    {
        if (!(frame_bitmap[i / 32] & (1 << (i % 32))))
        {
            frame_bitmap[i / 32] |= (1 << (i % 32));
            next_free_frame = i + 1;
            return i * FRAME_SIZE;
        }
    }

    for (uint32_t i = 0; i < next_free_frame; i++)
    {
        if (!(frame_bitmap[i / 32] & (1 << (i % 32))))
        {
            frame_bitmap[i / 32] |= (1 << (i % 32));
            return i * FRAME_SIZE;
        }
    }

    return 0xFFFFFFFF;
}

static void free_frame(uint32_t addr)
{
    uint32_t frame = addr / FRAME_SIZE;
    if (frame < total_frames)
        frame_bitmap[frame / 32] &= ~(1 << (frame % 32));
}

uint64_t paging_alloc_frame(void)
{
    return alloc_frame();
}

void paging_init(uint64_t mem_size)
{
    __asm__ volatile("mov %%cr3, %0" : "=r"(kernel_cr3));
    if (mem_size == 0)
        mem_size = 128 * 1024 * 1024;

    total_frames = mem_size / FRAME_SIZE;
    uint32_t bitmap_size = (total_frames + 31) / 32;

    frame_bitmap = (uint32_t*)malloc(bitmap_size * 4);
    memset(frame_bitmap, 0, bitmap_size * 4);

    for (uint32_t i = 0; i < 16; i++)
        frame_bitmap[i / 32] |= (1 << (i % 32));

    printf("[PAGING] Memory management initialized, %d MB available\n",
           mem_size / (1024 * 1024));
}

void map_page(uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t pml4_entry = (virt >> 39) & 0x1FF;
    uint64_t pdpt_entry = (virt >> 30) & 0x1FF;
    uint64_t pd_entry = (virt >> 21) & 0x1FF;
    uint64_t pt_entry = (virt >> 12) & 0x1FF;

    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t* pml4 = (uint64_t*)cr3;

    uint64_t* pdpt;
    if (!(pml4[pml4_entry] & 1))
    {
        uint64_t page = alloc_frame();
        memset((void*)page, 0, 4096);
        pml4[pml4_entry] = page | 0x07;
        pdpt = (uint64_t*)page;
    }
    else
        pdpt = (uint64_t*)(pml4[pml4_entry] & ~0xFFF);

    uint64_t* pd;
    if (!(pdpt[pdpt_entry] & 1))
    {
        uint64_t page = alloc_frame();
        memset((void*)page, 0, 4096);
        pdpt[pdpt_entry] = page | 0x07;
        pd = (uint64_t*)page;
    }
    else
        pd = (uint64_t*)(pdpt[pdpt_entry] & ~0xFFF);

    uint64_t* pt;
    if (!(pd[pd_entry] & 1))
    {
        uint64_t page = alloc_frame();
        memset((void*)page, 0, 4096);
        pd[pd_entry] = page | 0x07;
        pt = (uint64_t*)page;
    }
    else
        pt = (uint64_t*)(pd[pd_entry] & ~0xFFF);

    pt[pt_entry] = (phys & ~0xFFF) | (flags & 0xFFF) | 1;

    __asm__ volatile("invlpg (%0)" : : "r"(virt));
}

void unmap_page(uint64_t virt)
{
    uint64_t pml4_entry = (virt >> 39) & 0x1FF;
    uint64_t pdpt_entry = (virt >> 30) & 0x1FF;
    uint64_t pd_entry = (virt >> 21) & 0x1FF;
    uint64_t pt_entry = (virt >> 12) & 0x1FF;

    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t* pml4 = (uint64_t*)cr3;
    if (!(pml4[pml4_entry] & 1)) return;

    uint64_t* pdpt = (uint64_t*)(pml4[pml4_entry] & ~0xFFF);
    if (!(pdpt[pdpt_entry] & 1)) return;

    uint64_t* pd = (uint64_t*)(pdpt[pdpt_entry] & ~0xFFF);
    if (!(pd[pd_entry] & 1)) return;

    uint64_t* pt = (uint64_t*)(pd[pd_entry] & ~0xFFF);
    pt[pt_entry] = 0;

    __asm__ volatile("invlpg (%0)" : : "r"(virt));
}

uint64_t paging_create_pml4(void)
{
    uint64_t page = alloc_frame();
    memset((void*)page, 0, 4096);

    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t* pml4 = (uint64_t*)cr3;
    uint64_t* new_pml4 = (uint64_t*)page;

    for (int i = 0; i < 512; i++)
    {
        new_pml4[i] = pml4[i];
        if (new_pml4[i] & 1)
            new_pml4[i] |= 0x06;
    }

    return page;
}

int paging_map_user(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t pml4_entry = (virt >> 39) & 0x1FF;
    uint64_t pdpt_entry = (virt >> 30) & 0x1FF;
    uint64_t pd_entry = (virt >> 21) & 0x1FF;
    uint64_t pt_entry = (virt >> 12) & 0x1FF;

    uint64_t* pml4 = (uint64_t*)pml4_phys;

    uint64_t* pdpt;
    if (!(pml4[pml4_entry] & 1))
    {
        uint64_t page = alloc_frame();
        memset((void*)page, 0, 4096);
        pml4[pml4_entry] = page | 0x07;
        pdpt = (uint64_t*)page;
    }
    else
    {
        pdpt = (uint64_t*)(pml4[pml4_entry] & ~0xFFF);
        pml4[pml4_entry] |= 0x06;
    }

    uint64_t* pd;
    if (!(pdpt[pdpt_entry] & 1))
    {
        uint64_t page = alloc_frame();
        memset((void*)page, 0, 4096);
        pdpt[pdpt_entry] = page | 0x07;
        pd = (uint64_t*)page;
    }
    else
    {
        pd = (uint64_t*)(pdpt[pdpt_entry] & ~0xFFF);
        pdpt[pdpt_entry] |= 0x06;
    }

    uint64_t* pt;
    if (!(pd[pd_entry] & 1))
    {
        uint64_t page = alloc_frame();
        memset((void*)page, 0, 4096);
        pd[pd_entry] = page | 0x07;
        pt = (uint64_t*)page;
    }
    else if (pd[pd_entry] & (1 << 7))
    {
        // 2MB huge page detected — split into 4KB page table
        uint64_t huge_base = pd[pd_entry] & ~((1ULL << 21) - 1);
        uint64_t huge_flags = pd[pd_entry] & 0x1FF;

        uint64_t new_pt = alloc_frame();
        uint64_t* pt_virt = (uint64_t*)new_pt;
        for (int j = 0; j < 512; j++)
            pt_virt[j] = (huge_base + j * 0x1000) | (huge_flags & ~(1ULL << 7)) | 1;

        pd[pd_entry] = new_pt | 0x07;
        pt = pt_virt;
    }
    else
        pt = (uint64_t*)(pd[pd_entry] & ~0xFFF);

    pt[pt_entry] = (phys & ~0xFFF) | (flags & 0xFFF) | 1;

    __asm__ volatile("invlpg (%0)" : : "r"(virt));
    return 0;
}

uint32_t paging_get_total_frames(void)
{
    return total_frames;
}

uint32_t paging_get_used_frames(void)
{
    uint32_t used = 0;
    for (uint32_t i = 0; i < total_frames; i++)
    {
        if (frame_bitmap[i / 32] & (1 << (i % 32)))
            used++;
    }
    return used;
}

void paging_switch(uint64_t cr3)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}
