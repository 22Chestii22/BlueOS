#include "types.h"
#include "string.h"
#include "screen.h"

#define GDT_CODE_RING0 0x0020980000000000ULL
#define GDT_DATA_RING0 0x0000920000000000ULL
#define GDT_CODE_RING3 0x0020F80000000000ULL
#define GDT_DATA_RING3 0x0000F20000000000ULL

enum { GDT_NULL, GDT_CODE0, GDT_DATA0, GDT_DATA3, GDT_CODE3, GDT_TSSL, GDT_TSSH };

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed)) tss_t;

static tss_t tss;
static uint64_t gdt[7];
static uint8_t ist_stack[4096] __attribute__((aligned(16)));
static uint8_t gp_stack[4096] __attribute__((aligned(16)));
static uint8_t pf_stack[4096] __attribute__((aligned(16)));

static uint64_t tss_desc(uint64_t base, uint32_t limit)
{
    uint64_t d = 0;
    d |= (limit & 0xFFFF);
    d |= ((base & 0xFFFFFF) << 16);
    d |= (0x89ULL << 40);
    d |= (((uint64_t)((limit >> 16) & 0x0F)) << 48);
    d |= (((uint64_t)((base >> 24) & 0xFF)) << 56);
    return d;
}

void gdt_init(void)
{
    memset(&tss, 0, sizeof(tss));
    tss.iopb_offset = sizeof(tss);
    tss.ist[0] = (uint64_t)ist_stack + sizeof(ist_stack);
    tss.ist[1] = (uint64_t)gp_stack + sizeof(gp_stack);
    tss.ist[2] = (uint64_t)pf_stack + sizeof(pf_stack);
    
    for (int i = 0; i < 7; i++) gdt[i] = 0;
    gdt[GDT_CODE0] = GDT_CODE_RING0;
    gdt[GDT_DATA0] = GDT_DATA_RING0;
    gdt[GDT_DATA3] = GDT_DATA_RING3;
    gdt[GDT_CODE3] = GDT_CODE_RING3;

    uint64_t tb = (uint64_t)&tss;
    gdt[GDT_TSSL] = tss_desc(tb, sizeof(tss) - 1);
    gdt[GDT_TSSH] = tb >> 32;

    struct { uint16_t lim; uint64_t base; } __attribute__((packed)) gdtr = {
        .lim = sizeof(gdt) - 1, .base = (uint64_t)&gdt
    };

    __asm__ volatile("lgdt %0" : : "m"(gdtr));

    __asm__ volatile(
        "mov $0x10, %%ax\n mov %%ax, %%ds\n mov %%ax, %%es\n mov %%ax, %%ss\n"
        : : : "ax"
    );

    __asm__ volatile("ltr %w0" : : "r"((uint16_t)(GDT_TSSL * 8)));

    printf("[GDT] ring3 CS=0x23 DS=0x1B TSS=0x28\n");
}

extern uint64_t cpu_data[4];

void gdt_set_kernel_stack(uint64_t rsp0)
{
    tss.rsp0 = rsp0;
    cpu_data[3] = rsp0;
}
