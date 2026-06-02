BITS 64

global __syscall0
global __syscall1
global __syscall2
global __syscall3
global __syscall4
global __syscall5
global __syscall6

section .text

__syscall0:
    mov rax, rdi
    syscall
    ret

__syscall1:
    mov rax, rdi
    mov rdi, rsi
    syscall
    ret

__syscall2:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    syscall
    ret

__syscall3:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    syscall
    ret

__syscall4:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    syscall
    ret

__syscall5:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    mov r8, r9
    syscall
    ret

__syscall6:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    mov r8, r9
    mov r9, [rsp + 8] ; 6th argument (7th parameter of function) is on stack
    syscall
    ret
