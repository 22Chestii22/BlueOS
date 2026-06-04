#include "types.h"
#include "string.h"
#include "screen.h"
#include "process.h"
#include "mem.h"
#include "vfs.h"
#include "module.h"
#include "timer.h"
#include "gui.h"
#include "pe.h"
#include "serial.h"
extern void syscall_stub_handler(void);

int syscall_exec(uint64_t entry, const char* name)
{
    uint32_t pid = process_create(name, entry, 1);
    return pid;
}

uint64_t handle_syscall(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3,
                                uint64_t a4, uint64_t a5, uint64_t a6)
{
    UNUSED(a4);
    UNUSED(a5);
    UNUSED(a6);

    switch (n)
    {
        case 0:
            printf("[SYSCALL] write(%d, 0x%x, %d)\n", (int)a1, a2, (int)a3);
            return 0;

        case 1:
            process_exit((int)a1);
            return 0;

        case 2:
            return process_get_pid();

        case 3:
            printf("%s", (const char*)a1);
            return 0;

        case 4:
        {
            void* ptr = malloc((uint32_t)a1);
            return (uint64_t)ptr;
        }

        case 5:
            free((void*)a1);
            return 0;

        case 6:
            return vfs_open((const char*)a1, (int)a2);

        case 7:
            return vfs_read((int)a1, (void*)a2, (uint32_t)a3);

        case 8:
            return vfs_write((int)a1, (const void*)a2, (uint32_t)a3);

        case 9:
            return vfs_close((int)a1);

        case 10:
            return keyb_getchar_wrapper();

        case 32:
            return keyb_char_avail_wrapper();

        case 11:
            screen_clear();
            return 0;

        case 12:
            printf("[SYSCALL] exec(%s)\n", (const char*)a1);
            return 0;

        case 13:
            timer_sleep(a1);
            return 0;

        case 14:
            return gui_create_terminal((const char*)a1, (int)a4, (int)a5);

        case 15:
            gui_clear_terminal();
            return 0;

        case 16:
        {
            char buf[4096];
            int r = vfs_readdir((const char*)a1, buf, sizeof(buf));
            uint32_t max = (uint32_t)a3;
            memset((void*)a2, 0, max);
            if (r > 0)
            {
                uint32_t to_copy = (uint32_t)r;
                if (to_copy > max) to_copy = max;
                memcpy((void*)a2, buf, to_copy);
            }
            return r;
        }

        case 17:
            return pe_check_format((const char*)a1) ? 1 : 0;

        case 18:
        {
            int pid = pe_spawn((const char*)a1);
            if (pid > 0)
            {
                process_wait((uint32_t)pid);
                return 0;
            }
            return -1;
        }

        case 19:
            return vfs_exists((const char*)a1) ? 1 : 0;

        case 20:
            return gui_create((const char*)a1, (int)a4, (int)a5);

        case 21:
            return 0; // unused

        case 22:
            gui_puts((int)a1, (const char*)a2);
            return 0;

        case 23:
            gui_putchar((int)a1, (char)a2);
            return 0;

        case 24:
            gui_clear((int)a1);
            return 0;

        case 25:
            gui_set_title((int)a1, (const char*)a2);
            return 0;

        case 26:
        {
            int rect[4];
            gui_get_window_rect((int)a1, &rect[0], &rect[1], &rect[2], &rect[3]);
            if (a2)
                memcpy((void*)a2, rect, sizeof(rect));
            return 0;
        }

        case 27:
        {
            gui_event_t ev;
            int t = gui_get_event((int)a1, &ev);
            if (t && a2)
            {
                memcpy((void*)a2, &ev, sizeof(ev));
            }
            return t;
        }

        case 28:
            yield_to_scheduler();
            return 0;

        case 29:
            gui_render();
            return 0;

        case 30:
            gui_draw_rect((int)a1, (int)a2, (int)a3, (int)a4, (int)a5, (uint32_t)a6);
            return 0;

        case 31:
            gui_draw_text((int)a1, (int)a2, (int)a3, (const char*)a4, (uint32_t)a5, (uint32_t)a6);
            return 0;

        default:
            printf("[SYSCALL] Unknown syscall %d\n", (int)n);
            return -1;
    }
}

uint64_t __attribute__((aligned(64))) cpu_data[4];

void syscall_init(void)
{
    uint32_t low, high;

    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xC0000081));

    low = 0x00000000;
    high = 0x00100008;
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(0xC0000081));

    uint64_t lstar = (uint64_t)syscall_stub_handler;
    low = lstar & 0xFFFFFFFF;
    high = (lstar >> 32) & 0xFFFFFFFF;
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(0xC0000082));

    __asm__ volatile("wrmsr" : : "a"(0x7700), "d"(0), "c"(0xC0000083));

    cpu_data[2] = 0;
    cpu_data[3] = 0;

    low = 0;
    high = 0;
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(0xC0000101));

    low = (uint64_t)&cpu_data & 0xFFFFFFFF;
    high = ((uint64_t)&cpu_data >> 32) & 0xFFFFFFFF;
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(0xC0000102));

    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xC0000080));
    low |= 1;
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(0xC0000080));

    printf("[SYSCALL] syscall/sysret initialized (GS.base=0x%x, kernel_GS.base=0x%x)\n",
           &cpu_data, &cpu_data);
}
