BITS 64

; Syscall convention (Linux-compatible):
;   rax = syscall number
;   rdi = arg1, rsi = arg2, rdx = arg3
;   r10 = arg4, r8 = arg5, r9 = arg6
; SYSCALL clobbers: rcx = rip, r11 = rflags
; All other registers preserved.

extern handle_syscall

global syscall_stub_handler
syscall_stub_handler:
    swapgs
    mov gs:0x10, rsp
    mov rsp, gs:0x18

    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    mov rdi, rax
    mov rsi, [rsp + 40]
    mov rdx, [rsp + 32]
    mov rcx, [rsp + 24]
    mov r8,  [rsp + 72]
    mov r9,  [rsp + 56]
    call handle_syscall

    mov [rsp], rax
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    mov rsp, gs:0x10
    swapgs
    o64 sysret
