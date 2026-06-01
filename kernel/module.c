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

static char (*registered_keyb_getchar)(void) = NULL;
static uint64_t (*registered_timer_get_ticks)(void) = NULL;

static void reg_keyb(char (*func)(void))
{
    registered_keyb_getchar = func;
}

static void reg_timer(uint64_t (*func)(void))
{
    registered_timer_get_ticks = func;
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
    .register_timer_get_ticks = reg_timer,
    .ata_read_sectors = ata_read_sectors,
    .ata_write_sectors = ata_write_sectors,

    .fb_width = 0,
    .fb_height = 0,
};

char keyb_getchar_wrapper(void)
{
    if (registered_keyb_getchar)
        return (*registered_keyb_getchar)();
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
