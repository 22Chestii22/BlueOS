#include "types.h"
#include "string.h"
#include "screen.h"

#include "process.h"
#include "scheduler.h"
#include "mem.h"
#include "paging.h"
#include "gdt.h"

static process_t process_table[MAX_PROCESSES];
static uint32_t next_pid = 1;
static process_t* current_process = NULL;
static process_t* ready_queue = NULL;
static process_t* ready_queue_end = NULL;

context_t idle_context;

void process_init(void)
{
    memset(process_table, 0, sizeof(process_table));
    printf("[PROC] Process manager initialized\n");
}

static process_t* find_free_slot(void)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (process_table[i].state == PROCESS_TERMINATED ||
            process_table[i].state == PROCESS_CREATED)
            return &process_table[i];
    }
    return NULL;
}

static void add_to_ready_queue(process_t* proc)
{
    proc->next = NULL;
    if (!ready_queue)
    {
        ready_queue = proc;
        ready_queue_end = proc;
    }
    else
    {
        ready_queue_end->next = proc;
        ready_queue_end = proc;
    }
}

static void remove_from_ready_queue(process_t* proc)
{
    if (!ready_queue) return;

    if (ready_queue == proc)
    {
        ready_queue = proc->next;
        if (!ready_queue) ready_queue_end = NULL;
        proc->next = NULL;
        return;
    }

    process_t* p = ready_queue;
    while (p->next)
    {
        if (p->next == proc)
        {
            p->next = proc->next;
            if (!p->next) ready_queue_end = p;
            proc->next = NULL;
            return;
        }
        p = p->next;
    }
}

void process_cleanup(process_t* proc)
{
    if (!proc) return;
    if (proc->kernel_stack)
    {
        free(proc->kernel_stack);
        proc->kernel_stack = NULL;
    }
    if (proc->context)
    {
        free(proc->context);
        proc->context = NULL;
    }
    if (proc->page_table)
    {
        paging_free_pml4(proc->page_table);
        proc->page_table = 0;
    }
}

uint32_t process_create(const char* name, uint64_t entry, int user)
{
    printf("[PROC:CREATE] name=%s entry=0x%x user=%d\n", name, entry, user);

    process_t* proc = find_free_slot();
    if (!proc) return 0;
    printf("[PROC:CREATE] slot found\n");

    if (proc->state == PROCESS_TERMINATED)
    {
        process_cleanup(proc);
    }
    memset(proc, 0, sizeof(process_t));
    strncpy(proc->name, name, PROCESS_NAME_MAX - 1);
    proc->pid = next_pid++;
    proc->state = PROCESS_READY;
    proc->entry_point = entry;
    proc->user_stack = 0;

    proc->context = (context_t*)malloc(sizeof(context_t));
    if (!proc->context) return 0;
    memset(proc->context, 0, sizeof(context_t));
    printf("[PROC:CREATE] context=0x%x\n", (uint64_t)proc->context);

    proc->kernel_stack = (uint64_t*)malloc(STACK_SIZE);
    if (!proc->kernel_stack) { free(proc->context); return 0; }
    proc->kernel_stack_size = STACK_SIZE;
    printf("[PROC:CREATE] kernel_stack=0x%x\n", (uint64_t)proc->kernel_stack);

    uint64_t kstack_top = (uint64_t)proc->kernel_stack + STACK_SIZE;

    if (user)
    {
        printf("[PROC:CREATE] allocating user stack...\n");

        printf("[PROC:CREATE] creating PML4...\n");
        uint64_t pml4 = paging_create_pml4();
        proc->page_table = pml4;
        printf("[PROC:CREATE] PML4=0x%x\n", pml4);

        uint64_t stack_virt = 0x70000000ULL;
        printf("[PROC:CREATE] mapping stack...\n");
        for (uint64_t off = 0; off < 0x200000; off += 0x1000)
        {
            uint64_t stack_phys = paging_alloc_frame();
            paging_map_user(pml4, stack_virt + off, stack_phys, 0x007);
        }
        proc->user_stack = stack_virt + 0x200000;
        printf("[PROC:CREATE] stack mapped\n");

        proc->context->rip = entry;
        proc->context->cs = 0x23;
        proc->context->rsp = stack_virt + 0x200000;
        proc->context->ss = 0x1B;
        proc->context->rflags = 0x200;

    }
    else
    {
        proc->context->rip = entry;
        proc->context->cs = 0x08;
        proc->context->rsp = kstack_top;
        proc->context->ss = 0x10;
        proc->context->rflags = 0x202;
    }

    add_to_ready_queue(proc);

    printf("[PROC] Created PID %d '%s' at 0x%x (%s)\n",
           proc->pid, name, entry, user ? "ring 3" : "ring 0");
    return proc->pid;
}

int process_kill(uint32_t pid)
{
    if (pid == 0 || pid >= next_pid) return -1;

    process_t* proc = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (process_table[i].pid == pid)
        {
            proc = &process_table[i];
            break;
        }
    }

    if (!proc || proc->state == PROCESS_TERMINATED) return -1;

    remove_from_ready_queue(proc);
    proc->state = PROCESS_TERMINATED;
    proc->exit_code = -1;

    process_cleanup(proc);

    return 0;
}

process_t* process_get_current(void)
{
    return current_process;
}

uint32_t process_get_pid(void)
{
    if (current_process)
        return current_process->pid;
    return 0;
}

void process_exit(int code)
{
    if (current_process)
    {
        current_process->state = PROCESS_TERMINATED;
        current_process->exit_code = code;
        remove_from_ready_queue(current_process);
    }
    schedule();
    for (;;)
        __asm__ volatile("hlt");
}

void process_set_current(process_t* proc)
{
    current_process = proc;
}

process_t* process_get_by_pid(uint32_t pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (process_table[i].pid == pid && process_table[i].state != PROCESS_TERMINATED)
            return &process_table[i];
    }
    return NULL;
}

process_t* process_get_ready(void)
{
    if (!ready_queue) return NULL;
    process_t* proc = ready_queue;
    ready_queue = ready_queue->next;
    if (!ready_queue) ready_queue_end = NULL;
    proc->next = NULL;
    return proc;
}

int process_is_alive(uint32_t pid)
{
    return process_get_by_pid(pid) != NULL;
}

extern void yield_to_scheduler(void);
extern void context_activate(context_t* ctx, uint64_t kernel_stack_top);
extern uint64_t kernel_cr3;
extern uint64_t cpu_data[4];

void yield_handler(context_t* frame)
{
    process_t* current = process_get_current();
    if (!current) return;

    for (int i = 0; i < 20; i++)
        ((uint64_t*)current->context)[(i + 15) % 20] = ((uint64_t*)frame)[i];

    current->state = PROCESS_READY;
    add_to_ready_queue(current);

    process_t* next = process_get_ready();
    if (!next)
    {
        current->state = PROCESS_RUNNING;
        return;
    }

    next->state = PROCESS_RUNNING;
    process_set_current(next);

    uint64_t kstack = (uint64_t)next->kernel_stack + next->kernel_stack_size;
    gdt_set_kernel_stack(kstack);
    cpu_data[3] = kstack;

    if (next->page_table)
        paging_switch(next->page_table);
    else
        paging_switch(kernel_cr3);

    context_activate(next->context, kstack);
    // never reached
}

void process_wait(uint32_t pid)
{
    while (process_is_alive(pid))
        yield_to_scheduler();

    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (process_table[i].pid == pid)
        {
            process_cleanup(&process_table[i]);
            break;
        }
    }
}

void process_yield(void)
{
    if (current_process && current_process->state == PROCESS_RUNNING)
    {
        current_process->state = PROCESS_READY;
        add_to_ready_queue(current_process);
    }
}
