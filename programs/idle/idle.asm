BITS 64

SYSCALL_YIELD equ 28

section .text

start:
.loop:
    mov rax, SYSCALL_YIELD
    syscall
    jmp .loop
