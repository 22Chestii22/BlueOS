#include "types.h"
#include "screen.h"
#include "process.h"
#include "mem.h"
#include "paging.h"
#include "gdt.h"
#include "timer.h"

extern void process_switch(context_t** old, context_t* new);

static volatile int scheduling_enabled = 0;

void scheduler_init(void)
{
    scheduling_enabled = 1;
    printf("[SCHED] Scheduler initialized\n");
}

void schedule(void)
{
    if (!scheduling_enabled) return;

    process_t* current = process_get_current();
    process_t* next = NULL;

    if (current && current->state == PROCESS_RUNNING)
        process_yield();

    next = process_get_ready();

    if (!next)
    {
        if (current && current->state == PROCESS_READY)
        {
            current->state = PROCESS_RUNNING;
            process_set_current(current);
        }
        return;
    }

    if (current && current != next && current->state != PROCESS_TERMINATED)
    {
        current->state = PROCESS_READY;
    }

    next->state = PROCESS_RUNNING;
    process_set_current(next);

    uint64_t kstack = (uint64_t)next->kernel_stack + next->kernel_stack_size;
    gdt_set_kernel_stack(kstack);

    if (next->page_table)
        paging_switch(next->page_table);

    if (current && current->state != PROCESS_TERMINATED)
        process_switch(&current->context, next->context);
    else
    {
        void* dummy;
        process_switch((context_t**)&dummy, next->context);
    }
}

void scheduler_tick(void)
{
    if (scheduling_enabled)
        schedule();
}

void scheduler_enable(void)
{
    scheduling_enabled = 1;
}

void scheduler_disable(void)
{
    scheduling_enabled = 0;
}
