BITS 64

section .text

start:
    mov rax, 3
    lea rdi, [rel banner]
    syscall

    mov r12, 5

.loop:
    cmp r12, 5
    je .print5
    cmp r12, 4
    je .print4
    cmp r12, 3
    je .print3
    cmp r12, 2
    je .print2
    cmp r12, 1
    je .print1
    jmp .boom

.print5:
    mov rax, 3
    lea rdi, [rel msg5]
    syscall
    jmp .wait

.print4:
    mov rax, 3
    lea rdi, [rel msg4]
    syscall
    jmp .wait

.print3:
    mov rax, 3
    lea rdi, [rel msg3]
    syscall
    jmp .wait

.print2:
    mov rax, 3
    lea rdi, [rel msg2]
    syscall
    jmp .wait

.print1:
    mov rax, 3
    lea rdi, [rel msg1]
    syscall

.wait:
    mov rax, 13
    mov rdi, 100
    syscall

    dec r12
    jnz .loop

.boom:
    mov rax, 3
    lea rdi, [rel msg0]
    syscall

    mov rax, 1
    xor rdi, rdi
    syscall

    pause
    jmp $

banner: db "[COUNT] BlueOS Countdown!", 0xD, 0xA, 0xD, 0xA, 0
msg5:   db "[COUNT] 5...", 0xD, 0xA, 0
msg4:   db "[COUNT] 4...", 0xD, 0xA, 0
msg3:   db "[COUNT] 3...", 0xD, 0xA, 0
msg2:   db "[COUNT] 2...", 0xD, 0xA, 0
msg1:   db "[COUNT] 1...", 0xD, 0xA, 0
msg0:   db "[COUNT] BOOM! Time's up!", 0xD, 0xA, 0
