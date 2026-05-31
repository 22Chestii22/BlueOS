BITS 64

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
    lea rsp, [rsi + 16]
    push qword [rdi + 19*8]
    push qword [rdi + 18*8]
    push qword [rdi + 17*8]
    push qword [rdi + 16*8]
    push qword [rdi + 15*8]
    jmp .push_regs

.ring3:
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
