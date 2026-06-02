BITS 64

SYSCALL_GUI_RENDER equ 29
SYSCALL_YIELD      equ 28

section .text

start:
.loop:
    mov rax, SYSCALL_GUI_RENDER
    syscall
    mov rax, SYSCALL_YIELD
    syscall
    jmp .loop
