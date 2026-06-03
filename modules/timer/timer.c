#include "types.h"
#include "kernel_api.h"
#include "process.h"
#include "paging.h"
#include "gdt.h"

extern uint64_t cpu_data[4];

static kernel_api_t* api = NULL;
static volatile uint64_t tick_count = 0;
static volatile int scheduling_enabled = 0;

extern void context_activate(context_t* ctx, uint64_t kernel_stack_top);

static void timer_eoi(void)
{
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x20), "Nd"((uint16_t)0x20));
}

void timer_handler_and_schedule(context_t* frame)
{
    tick_count++;

    if (!api || !scheduling_enabled)
    {
        timer_eoi();
        return;
    }

    api->outb(0x20, 0x20);

    process_t* current = process_get_current();
    if (!current || current->state != PROCESS_RUNNING) return;

    if ((frame->cs & 3) == 0) return;

    current->user_rsp = frame->rsp;

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

    uint64_t* ctx = (uint64_t*)current->context;
    uint64_t* frm = (uint64_t*)frame;
    for (int i = 0; i < 20; i++)
        ctx[i] = frm[i];

    next->state = PROCESS_RUNNING;
    process_set_current(next);

    uint64_t kstack = (uint64_t)next->kernel_stack + next->kernel_stack_size;

    gdt_set_kernel_stack(kstack);
    cpu_data[3] = kstack;
    cpu_data[2] = next->user_rsp;

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
    while (tick_count - start < ms)
        yield_to_scheduler();
}


void timer_scheduler_enable(void)
{
    scheduling_enabled = 1;
}

void timer_init(int frequency)
{
    int divisor = 1193180 / frequency;
    api->outb(0x43, 0x36);
    api->outb(0x40, (uint8_t)(divisor & 0xFF));
    api->outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void timer_module_init(kernel_api_t* kapi)
{
    api = kapi;
    api->irq_install_handler(0, (void*)timer_handler);
    api->printf("[TIMER] Module loaded (not yet started)\n");
}

void timer_start(void)
{
    timer_init(100);
}
