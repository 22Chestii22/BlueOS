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
#include "serial.h"
#include "gdt.h"
#include "paging.h"
#include "pe.h"
#include "module.h"
#include "vga.h"
#include "fb.h"
#include "gui.h"
#include "timer.h"

extern void idt_init(void);
extern void paging_init(uint64_t mem_size);
extern void scheduler_init(void);
extern void context_activate(context_t* ctx, uint64_t kernel_stack_top);
extern uint64_t cpu_data[4];

extern void mouse_module_init(kernel_api_t* api);

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

static int mb2_find_framebuffer(void* mbd, uint64_t* fb_addr, uint32_t* fb_width, uint32_t* fb_height, uint32_t* fb_pitch, uint8_t* fb_bpp)
{
    if (!mbd) return -1;
    uint64_t total = *(uint32_t*)mbd;
    if (total < 16) return -1;

    uint64_t pos = 8;
    while (pos + 8 <= total)
    {
        uint32_t type = *(uint32_t*)((uint64_t)mbd + pos);
        uint32_t size = *(uint32_t*)((uint64_t)mbd + pos + 4);
        if (size < 8) break;
        if (type == 0) break;

        if (type == 8)
        {
            *fb_addr = *(uint64_t*)((uint64_t)mbd + pos + 8);
            *fb_pitch = *(uint32_t*)((uint64_t)mbd + pos + 16);
            *fb_width = *(uint32_t*)((uint64_t)mbd + pos + 20);
            *fb_height = *(uint32_t*)((uint64_t)mbd + pos + 24);
            *fb_bpp = *(uint8_t*)((uint64_t)mbd + pos + 28);
            return 0;
        }

        pos += (size + 7) & ~7;
    }
    return -1;
}

void kernel_main(void* mbd, uint32_t magic)
{
    UNUSED(magic);

    serial_init();

    uint64_t mem_size = mb2_get_mem_size(mbd);

    screen_set_color(COLOR_WHITE, COLOR_BLACK);
    screen_write("BlueOS x86_64 - Initializing...\n");

    mem_init();
    paging_init(mem_size);
    gdt_init();
    idt_init();

    // Try to initialize framebuffer from multiboot2
    uint64_t fb_addr = 0;
    uint32_t fb_width = 0, fb_height = 0, fb_pitch = 0;
    uint8_t fb_bpp = 0;

    if (mb2_find_framebuffer(mbd, &fb_addr, &fb_width, &fb_height, &fb_pitch, &fb_bpp) == 0)
    {
        screen_write("Framebuffer found!\n");
        screen_write("  Address: 0x"); screen_write_hex(fb_addr); screen_write("\n");
        screen_write("  Width:   "); screen_write_dec(fb_width); screen_write("\n");
        screen_write("  Height:  "); screen_write_dec(fb_height); screen_write("\n");
        screen_write("  BPP:     "); screen_write_dec(fb_bpp); screen_write("\n");

        fb_init(fb_addr, fb_width, fb_height, fb_pitch, fb_bpp);
    kernel_api.fb_width = fb_width;
    kernel_api.fb_height = fb_height;
    }
    else
    {
        screen_write("No framebuffer found!\n");
    }

    vga_init();
    gui_init();

    keyb_module_init(&kernel_api);
    timer_module_init(&kernel_api);
    ata_module_init(&kernel_api);
    fat_module_init(&kernel_api);
    mouse_module_init(&kernel_api);
    load_disk_modules("\\SYSTEM\\DRIVERS");
    process_init();
    syscall_init();

    scheduler_init();
    timer_start();
    timer_scheduler_enable();
    pe_spawn("\\SYSTEM\\PROGRAMS\\RENDER.EXE");
    pe_spawn("\\SYSTEM\\PROGRAMS\\CMD.EXE");
    pe_spawn("\\SYSTEM\\PROGRAMS\\IDLE.EXE");

    process_t* first = process_get_ready();
    if (first)
    {
        uint64_t kstack = (uint64_t)first->kernel_stack + first->kernel_stack_size;
        first->state = PROCESS_RUNNING;
        process_set_current(first);
        gdt_set_kernel_stack(kstack);
        cpu_data[3] = kstack;
        if (first->page_table)
            paging_switch(first->page_table);
        context_activate(first->context, kstack);
    }

    for (;;)
        __asm__ volatile("hlt");
}
