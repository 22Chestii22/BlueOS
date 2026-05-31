#include "types.h"
#include "string.h"
#include "screen.h"
#include "process.h"
#include "scheduler.h"
#include "mem.h"
#include "idt.h"
#include "vfs.h"
#include "fat.h"
#include "syscall.h"
#include "cmd.h"
#include "serial.h"
#include "gdt.h"
#include "paging.h"
#include "pe.h"
#include "module.h"

extern void idt_init(void);
extern void paging_init(uint64_t mem_size);
extern void scheduler_init(void);
extern void context_activate(context_t* ctx, uint64_t kernel_stack_top);

static void idle_task(void)
{
    for (;;)
        __asm__ volatile("hlt");
}

static uint64_t mb2_get_mem_size(void* mbd)
{
    if (!mbd) return 0;
    uint64_t total = *(uint32_t*)mbd;
    if (total < 16) return 0;

    uint64_t pos = 8;
    uint64_t max_mem = 0;

    while (pos + 8 <= total)
    {
        uint32_t type = *(uint32_t*)((uint64_t)mbd + pos);
        uint32_t size = *(uint32_t*)((uint64_t)mbd + pos + 4);
        if (size < 8) break;
        if (type == 0) break;

        if (type == 6)
        {
            uint32_t entry_size = *(uint32_t*)((uint64_t)mbd + pos + 8);
            uint32_t entries = (size - 16) / entry_size;
            uint64_t base = pos + 16;

            for (uint32_t i = 0; i < entries; i++)
            {
                uint64_t addr = *(uint64_t*)((uint64_t)mbd + base);
                uint64_t len = *(uint64_t*)((uint64_t)mbd + base + 8);
                uint32_t entry_type = *(uint32_t*)((uint64_t)mbd + base + 16);
                if (entry_type == 1)
                {
                    uint64_t end = addr + len;
                    if (end > max_mem) max_mem = end;
                }
                base += entry_size;
            }
        }

        pos += (size + 7) & ~7;
    }

    return max_mem;
}

void kernel_main(void* mbd, uint32_t magic)
{
    UNUSED(magic);

    serial_init();
    screen_clear();

    uint64_t mem_size = mb2_get_mem_size(mbd);

    screen_set_color(COLOR_CYAN, COLOR_BLACK);
    for (int i = 0; i < 80; i++) printf("=");
    printf("\n");
    screen_set_color(COLOR_WHITE, COLOR_BLACK);
    printf("                              BlueOS x86_64\n");
    screen_set_color(COLOR_CYAN, COLOR_BLACK);
    for (int i = 0; i < 80; i++) printf("=");
    printf("\n");
    screen_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    printf("\n");

    mem_init();
    paging_init(mem_size);
    gdt_init();
    idt_init();
    keyb_module_init(&kernel_api);
    timer_module_init(&kernel_api);
    ata_module_init(&kernel_api);
    fat_module_init(&kernel_api);
    process_init();
    syscall_init();

    scheduler_init();
    process_create("cmd.exe", (uint64_t)cmd_run, 0);
    process_create("idle", (uint64_t)idle_task, 0);

    process_t* first = process_get_ready();
    if (first)
    {
        uint64_t kstack = (uint64_t)first->kernel_stack + first->kernel_stack_size;
        first->state = PROCESS_RUNNING;
        process_set_current(first);
        gdt_set_kernel_stack(kstack);
        context_activate(first->context, kstack);
    }

    for (;;)
        __asm__ volatile("hlt");
}
