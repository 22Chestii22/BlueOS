#include "types.h"
#include "string.h"
#include "screen.h"
#include "mem.h"
#include "io.h"
#include "idt.h"
#include "module.h"
#include "ata.h"
#include "vfs.h"
#include "elf_loader.h"
#include "fb.h"
#include "process.h"
#include "paging.h"
#include "gdt.h"
#include "timer.h"

extern void context_activate(context_t* ctx, uint64_t kernel_stack_top);

static char (*registered_keyb_getchar)(void) = NULL;
static int (*registered_keyb_char_avail)(void) = NULL;
static uint64_t (*registered_timer_get_ticks)(void) = NULL;


static void reg_keyb(char (*func)(void))
{
    registered_keyb_getchar = func;
}

static void reg_keyb_char_avail(int (*func)(void))
{
    registered_keyb_char_avail = func;
}

static void reg_timer(uint64_t (*func)(void))
{
    registered_timer_get_ticks = func;
}

static uint64_t get_kernel_cr3(void)
{
    return kernel_cr3;
}

/* Hotkey callback (Ctrl+Space launcher) */
static void (*hotkey_callback)(void) = NULL;

static void hotkey_triggered(void)
{
    if (hotkey_callback)
        hotkey_callback();
}

static void register_hotkey_cb(void (*func)(void))
{
    hotkey_callback = func;
}

/* Mouse callback registration and wrappers */
static int (*registered_mouse_get_x)(void) = NULL;
static int (*registered_mouse_get_y)(void) = NULL;
static uint8_t (*registered_mouse_get_buttons)(void) = NULL;
static int (*registered_mouse_is_present)(void) = NULL;

static void reg_mouse_get_x(int (*func)(void)) { registered_mouse_get_x = func; }
static void reg_mouse_get_y(int (*func)(void)) { registered_mouse_get_y = func; }
static void reg_mouse_get_buttons(uint8_t (*func)(void)) { registered_mouse_get_buttons = func; }
static void reg_mouse_is_present(int (*func)(void)) { registered_mouse_is_present = func; }

int mouse_get_x_wrapper(void)
{
    if (registered_mouse_get_x) return registered_mouse_get_x();
    return 0;
}

int mouse_get_y_wrapper(void)
{
    if (registered_mouse_get_y) return registered_mouse_get_y();
    return 0;
}

uint8_t mouse_get_buttons_wrapper(void)
{
    if (registered_mouse_get_buttons) return registered_mouse_get_buttons();
    return 0;
}

int mouse_is_present_wrapper(void)
{
    if (registered_mouse_is_present) return registered_mouse_is_present();
    return 0;
}

void timer_isr_dispatch(context_t* frame)
{
    timer_handler_and_schedule(frame);
}

kernel_api_t kernel_api =
{
    .printf = printf,
    .screen_putchar = screen_putchar,
    .screen_write = screen_write,
    .screen_set_color = screen_set_color,
    .screen_clear = screen_clear,
    .screen_get_cursor = screen_get_cursor,
    .screen_set_cursor = screen_set_cursor,
    .malloc = malloc,
    .free = free,
    .memcpy = (void (*)(void*, const void*, uint32_t))memcpy,
    .memset = (void (*)(void*, int, uint32_t))memset,
    .strcmp = strcmp,
    .strcpy = strcpy,
    .strncpy = strncpy,
    .strlen = (int (*)(const char*))strlen,
    .strcat = strcat,
    .strchr = strchr,
    .strstr = strstr,
    .inb = inb,
    .outb = outb,
    .inw = inw,
    .outw = outw,
    .inl = inl,
    .outl = outl,
    .irq_install_handler = (void (*)(int, void*))irq_install_handler,
    .register_keyb_getchar = reg_keyb,
    .register_keyb_char_avail = reg_keyb_char_avail,
    .register_timer_get_ticks = reg_timer,
    .timer_scheduler_enable = timer_scheduler_enable,
    .ata_read_sectors = ata_read_sectors,
    .ata_write_sectors = ata_write_sectors,

    .fb_width = 0,
    .fb_height = 0,

    .yield_to_scheduler = yield_to_scheduler,

    .process_get_current = process_get_current,
    .process_yield_to_ready = process_yield,
    .process_get_ready = process_get_ready,
    .process_set_current = process_set_current,

    .gdt_set_kernel_stack = gdt_set_kernel_stack,

    .paging_switch = paging_switch,
    .paging_get_kernel_cr3 = get_kernel_cr3,

    .context_activate = context_activate,

    .register_mouse_get_x = reg_mouse_get_x,
    .register_mouse_get_y = reg_mouse_get_y,
    .register_mouse_get_buttons = reg_mouse_get_buttons,
    .register_mouse_is_present = reg_mouse_is_present,

    .hotkey_triggered = hotkey_triggered,
    .register_hotkey_callback = register_hotkey_cb,
};

char keyb_getchar_wrapper(void)
{
    if (registered_keyb_getchar)
        return (*registered_keyb_getchar)();
    return 0;
}

int keyb_char_avail_wrapper(void)
{
    if (registered_keyb_char_avail)
        return registered_keyb_char_avail();
    return 0;
}

uint64_t timer_get_ticks_wrapper(void)
{
    if (registered_timer_get_ticks)
        return (*registered_timer_get_ticks)();
    return 0;
}

void load_disk_modules(const char* dir_path)
{
    char entries[4096];
    int count = vfs_readdir(dir_path, entries, sizeof(entries));
    if (count <= 0)
    {
        printf("[LOADER] No modules found in '%s'\n", dir_path);
        return;
    }

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
            printf("[LOADER] '%s' loaded (entry=0x%x, size=%d)\n",
                   mod.name, (uint64_t)mod.entry_point, mod.size);
            if (elf_call_init(&mod, &kernel_api) == 0)
                printf("[LOADER] '%s' initialized\n", mod.name);
            loaded++;
        }
        else
        {
            printf("[LOADER] Failed to load '%s'\n", full_path);
        }
    }

    if (loaded == 0)
        printf("[LOADER] No .sys modules loaded from '%s'\n", dir_path);
    else
        printf("[LOADER] Loaded %d module(s) from '%s'\n", loaded, dir_path);
}
