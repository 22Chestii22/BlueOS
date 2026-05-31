#include "types.h"
#include "io.h"
#include "screen.h"
#include "process.h"
#include "paging.h"
#include "gdt.h"
#include "string.h"

static volatile uint64_t tick_count = 0;
static volatile int scheduling_enabled = 0;

extern void context_activate(context_t* ctx, uint64_t kernel_stack_top);

void timer_handler_and_schedule(context_t* frame)
{
    outb(0x20, 0x20);
    tick_count++;

    if (!scheduling_enabled) return;

    process_t* current = process_get_current();
    if (!current || current->state != PROCESS_RUNNING) return;

    if ((frame->cs & 3) == 0)
        return;

    process_yield();
    process_t* next = process_get_ready();

    if (!next)
    {
        current->state = PROCESS_RUNNING;
        process_set_current(current);
        return;
    }

    if (next == current)
    {
        next->state = PROCESS_RUNNING;
        return;
    }

    if ((frame->cs & 3) == 0)
    {
        uint64_t actual_rsp = (uint64_t)(frame) + 18 * 8;
        frame->rsp = actual_rsp;
        frame->ss = 0x10;
    }

    for (int i = 0; i < 20; i++)
        ((uint64_t*)current->context)[i] = ((uint64_t*)frame)[i];

    next->state = PROCESS_RUNNING;
    process_set_current(next);

    uint64_t kstack = (uint64_t)next->kernel_stack + next->kernel_stack_size;

    gdt_set_kernel_stack(kstack);

    if (next->page_table)
        paging_switch(next->page_table);
    else
        paging_switch(kernel_cr3);

    context_activate(next->context, kstack);
}

void timer_handler(void)
{
    tick_count++;
}

uint64_t timer_get_ticks(void)
{
    return tick_count;
}

void timer_sleep(uint64_t ms)
{
    uint64_t start = tick_count;
    while (tick_count - start < ms);
}

void timer_scheduler_enable(void)
{
    scheduling_enabled = 1;
}

void timer_init(int frequency)
{
    int divisor = 1193180 / frequency;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}
