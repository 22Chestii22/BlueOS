BITS 64

extern yield_handler
extern cpu_data

global process_switch
process_switch:
    mov rax, [rsp]
    mov [rdi], rsp

    mov rsp, rsi

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    test byte [rsp + 8], 3
    jnz .ps_ring3

    mov rax, [rsp]
    mov rbx, [rsp + 8]
    mov rcx, [rsp + 16]
    mov rdx, [rsp + 24]
    mov rsp, rdx
    push rcx
    push rbx
    push rax
    iretq

.ps_ring3:
    iretq

; context_activate(context_t* ctx, uint64_t kernel_stack_top)
; rdi = context pointer, rsi = kernel stack top for this process
; Rebuilds full CPU frame on kernel stack and iretqs to process
global context_activate
context_activate:
    test byte [rdi + 16*8], 3
    jnz .ring3

.ring0:
    mov rsp, [rdi + 18*8]        ; restore saved RSP from context
    mov rax, [rdi + 15*8]        ; save rip
    push rax                     ; push rip for ret
    mov rax, [rdi + 14*8]        ; rax
    mov rbx, [rdi + 13*8]        ; rbx
    mov rcx, [rdi + 12*8]        ; rcx
    mov rdx, [rdi + 11*8]        ; rdx
    mov rsi, [rdi + 10*8]        ; rsi
    mov rbp, [rdi + 8*8]         ; rbp
    mov r8,  [rdi + 7*8]         ; r8
    mov r9,  [rdi + 6*8]         ; r9
    mov r10, [rdi + 5*8]         ; r10
    mov r11, [rdi + 4*8]         ; r11
    mov r12, [rdi + 3*8]         ; r12
    mov r13, [rdi + 2*8]         ; r13
    mov r14, [rdi + 1*8]         ; r14
    mov r15, [rdi + 0*8]         ; r15
    push qword [rdi + 17*8]      ; push rflags
    mov rdi, [rdi + 9*8]         ; restore rdi last
    popfq                        ; restore rflags
    ret

.ring3:
    cli
    mov rsp, rsi

    ; Fix GS.base and MSR_KERNEL_GS_BASE before entering user mode.
    ; All user processes expect:
    ;   GS.base = 0 (for swapgs on next SYSCALL entry)
    ;   MSR_KERNEL_GS_BASE = &cpu_data (swapgs target)
    ; This handles three entry paths correctly:
    ;   1) SYSCALL yield (swapgs done: GS.base=&cpu_data, MSR_KERNEL_GS_BASE=0)
    ;   2) Timer IRQ  (no swapgs:    GS.base=0, MSR_KERNEL_GS_BASE=&cpu_data)
    ;   3) First activation from main.c (no swapgs: GS.base=0, MSR_KERNEL_GS_BASE=&cpu_data)

    lea rax, [rel cpu_data]
    mov rdx, rax
    shr rdx, 32
    mov rcx, 0xC0000102       ; MSR_KERNEL_GS_BASE
    wrmsr

    xor eax, eax
    xor edx, edx
    mov rcx, 0xC0000101       ; MSR_GS_BASE (sets both MSR and GS.base register)
    wrmsr

    push qword [rdi + 19*8]
    push qword [rdi + 18*8]
    push qword [rdi + 17*8]
    push qword [rdi + 16*8]
    push qword [rdi + 15*8]

.push_regs:
    push qword [rdi + 14*8]
    push qword [rdi + 13*8]
    push qword [rdi + 12*8]
    push qword [rdi + 11*8]
    push qword [rdi + 10*8]
    push qword [rdi + 9*8]
    push qword [rdi + 8*8]
    push qword [rdi + 7*8]
    push qword [rdi + 6*8]
    push qword [rdi + 5*8]
    push qword [rdi + 4*8]
    push qword [rdi + 3*8]
    push qword [rdi + 2*8]
    push qword [rdi + 1*8]
    push qword [rdi + 0*8]

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

; yield_to_scheduler(void)
; Saves all registers in context_t format matching timer ISR layout,
; calls yield_handler C function which copies frame to current->context,
; yields, and context_activates the next ready process.
; Returns only if no other process is ready.
global yield_to_scheduler
yield_to_scheduler:
    ; Save registers in ISR order: rax first (highest addr), r15 last (lowest addr among regs)
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Build iret frame in CPU order: ss first (highest addr), rip last (lowest addr among iret)
    push 0x10                    ; ss (context[19])
    lea rax, [rsp + 136]         ; rax = entry_RSP + 8 = RSP before `call yield_to_scheduler`
    push rax                     ; rsp (context[18])
    pushfq                       ; rflags (context[17])
    push 0x08                    ; cs (context[16])
    mov rax, [rsp + 152]         ; return address at orig = RSP + (15*8 + 4*8) = RSP + 152
    push rax                     ; rip (context[15])

    ; Call yield_handler with rdi pointing to rip (skip the return address call will push)
    mov rdi, rsp                 ; rdi = address of rip before call
    call yield_handler           ; call pushes ret addr, rdi still points to rip

    ; yield_handler returned (no other ready process), clean up and return
    add rsp, 5*8                 ; remove iret frame
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

; yield_from_user_syscall(void)
; Called from syscall_stub_handler for syscall 28 (yield from user mode).
; Builds a correct context frame with user-mode CS=0x23, SS=0x1B,
; user RIP (from saved rcx), user RFLAGS (from saved r11), user RSP (gs:0x10),
; and calls yield_handler to schedule the next process.
global yield_from_user_syscall
yield_from_user_syscall:
    ; Stack at entry (from stub's 15 pushes + return addr from call):
    ; [rsp+0]   = return address (to stub)
    ; [rsp+8]   = rax=28      (user rax = syscall number)
    ; [rsp+16]  = rbx         (user rbx)
    ; [rsp+24]  = rcx=RIP     (user RIP, saved by syscall)
    ; [rsp+32]  = rdx         (user rdx)
    ; [rsp+40]  = rsi         (user rsi)
    ; [rsp+48]  = rdi         (user rdi)
    ; [rsp+56]  = rbp         (user rbp)
    ; [rsp+64]  = r8          (user r8)
    ; [rsp+72]  = r9          (user r9)
    ; [rsp+80]  = r10         (user r10)
    ; [rsp+88]  = r11=RFLAGS  (user RFLAGS, saved by syscall)
    ; [rsp+96]  = r12         (user r12)
    ; [rsp+104] = r13         (user r13)
    ; [rsp+112] = r14         (user r14)
    ; [rsp+120] = r15         (user r15)

    ; Allocate frame area (20 qwords = 160 bytes)
    sub rsp, 20*8

    ; Reference to old stack: [rsp + 160 + 8 + offset]
    ; = [rsp + 168 + offset]
    ; where offset is: rax=0, rbx=8, rcx=RIP=16, rdx=24, rsi=32,
    ; rdi=40, rbp=48, r8=56, r9=64, r10=72, r11=RFLAGS=80,
    ; r12=88, r13=96, r14=104, r15=112

    ; Get user RSP from GS base (saved by syscall_stub_handler)
    swapgs
    push gs:0x10
    swapgs
    pop r10                     ; r10 = user RSP

    ; Build frame in yield_handler format:
    ; [RIP, CS, RFLAGS, RSP, SS, r15, r14, ..., rax]

    ; frame[0] = user RIP
    mov rax, [rsp + 168 + 16]
    mov [rsp + 0], rax

    mov qword [rsp + 8], 0x23   ; frame[1] = CS (ring 3)

    ; frame[2] = user RFLAGS
    mov rax, [rsp + 168 + 80]
    mov [rsp + 16], rax

    mov [rsp + 24], r10         ; frame[3] = user RSP

    mov qword [rsp + 32], 0x1B  ; frame[4] = SS (ring 3)

    ; frame[5] = r15 ... frame[14] = rdi
    mov rax, [rsp + 168 + 112]  ; r15
    mov [rsp + 40], rax
    mov rax, [rsp + 168 + 104]  ; r14
    mov [rsp + 48], rax
    mov rax, [rsp + 168 + 96]   ; r13
    mov [rsp + 56], rax
    mov rax, [rsp + 168 + 88]   ; r12
    mov [rsp + 64], rax
    mov rax, [rsp + 168 + 80]   ; r11 = RFLAGS
    mov [rsp + 72], rax
    mov rax, [rsp + 168 + 72]   ; r10
    mov [rsp + 80], rax
    mov rax, [rsp + 168 + 64]   ; r9
    mov [rsp + 88], rax
    mov rax, [rsp + 168 + 56]   ; r8
    mov [rsp + 96], rax
    mov rax, [rsp + 168 + 48]   ; rbp
    mov [rsp + 104], rax
    mov rax, [rsp + 168 + 40]   ; rdi
    mov [rsp + 112], rax
    mov rax, [rsp + 168 + 32]   ; rsi
    mov [rsp + 120], rax
    mov rax, [rsp + 168 + 24]   ; rdx
    mov [rsp + 128], rax
    mov rax, [rsp + 168 + 16]   ; rcx = user RIP
    mov [rsp + 136], rax
    mov rax, [rsp + 168 + 8]    ; rbx
    mov [rsp + 144], rax
    mov rax, [rsp + 168 + 0]    ; rax = 28
    mov [rsp + 152], rax

    ; Call yield_handler with rdi pointing to frame
    mov rdi, rsp
    call yield_handler

    ; yield_handler returns only if no other process is ready.
    ; Clean up and return to stub for normal syscall return.
    add rsp, 20*8
    ret


; switch_to_user_mode(uint64_t entry, uint64_t user_stack)
; rdi = entry point (user virtual address)
; rsi = user stack top
global switch_to_user_mode
switch_to_user_mode:
    cli
    push 0x1B
    push rsi
    push 0x202
    push 0x23
    push rdi

    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    xor rbp, rbp

    iretq
