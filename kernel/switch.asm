BITS 64

extern yield_handler

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
    swapgs
    mov gs:0x18, rsi
    swapgs
    mov rsp, rsi
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
