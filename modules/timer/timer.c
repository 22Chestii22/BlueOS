#include "types.h"
#include "kernel_api.h"

static kernel_api_t* api = NULL;

void module_entry(kernel_api_t* kapi)
{
    api = kapi;

    int divisor = 1193180 / 100;
    api->outb(0x43, 0x36);
    api->outb(0x40, (uint8_t)(divisor & 0xFF));
    api->outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));

    api->timer_scheduler_enable();

    api->printf("[TIMER] PIT at 100 Hz, scheduling enabled\n");
}
