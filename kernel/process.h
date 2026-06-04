#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"

#define MAX_PROCESSES 64
#define STACK_SIZE 16384
#define PROCESS_NAME_MAX 32

typedef enum
{
    PROCESS_CREATED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
} process_state_t;

typedef struct context
{
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
} context_t;

typedef struct process
{
    uint32_t pid;
    char name[PROCESS_NAME_MAX];
    process_state_t state;
    context_t* context;
    uint64_t* kernel_stack;
    uint64_t kernel_stack_size;
    uint64_t entry_point;
    uint64_t page_table;
    uint64_t user_stack;
    uint64_t user_rsp;
    int exit_code;
    struct process* next;
} process_t;

void process_init(void);
uint32_t process_create(const char* name, uint64_t entry, int user);
int process_kill(uint32_t pid);
process_t* process_get_current(void);
uint32_t process_get_pid(void);
void process_exit(int code);
void process_switch(context_t** old, context_t* new_ctx);
void process_set_current(process_t* proc);
process_t* process_get_ready(void);
void process_yield(void);
process_t* process_get_by_pid(uint32_t pid);
int process_is_alive(uint32_t pid);
void process_wait(uint32_t pid);
void yield_to_scheduler(void);
int process_get_count(void);
int process_get_info(int index, uint32_t* pid, char* name, uint32_t* state);

#endif
