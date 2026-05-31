BITS 64

section .text

start:
    mov rax, 3
    lea rdi, [rel banner]
    syscall

    mov rax, 3
    lea rdi, [rel t_syscall]
    syscall
    mov rax, 3
    lea rdi, [rel ok]
    syscall

    mov rax, 3
    lea rdi, [rel t_getpid]
    syscall
    mov rax, 2
    syscall
    mov r12, rax
    mov rax, 3
    lea rdi, [rel ok]
    syscall

    mov rax, 3
    lea rdi, [rel t_malloc]
    syscall
    mov rax, 4
    mov rdi, 128
    syscall
    mov r12, rax
    mov rax, 3
    lea rdi, [rel ok]
    syscall

    mov rax, 3
    lea rdi, [rel t_free]
    syscall
    mov rax, 5
    mov rdi, r12
    syscall
    mov rax, 3
    lea rdi, [rel ok]
    syscall

    mov rax, 3
    lea rdi, [rel t_malloc2]
    syscall
    mov rax, 4
    mov rdi, 65536
    syscall
    mov r12, rax
    mov rax, 3
    lea rdi, [rel ok]
    syscall

    mov rax, 5
    mov rdi, r12
    syscall

    mov rax, 3
    lea rdi, [rel all_done]
    syscall

    mov rax, 1
    xor rdi, rdi
    syscall

.loop:
    pause
    jmp .loop

banner:    db "[TEST] Program Test Suite v1.0", 0xD, 0xA, 0xD, 0xA, 0
t_syscall: db "[TEST] Syscall 3 (print) .......... ", 0
t_getpid:  db "[TEST] Syscall 2 (getpid) ......... ", 0
t_malloc:  db "[TEST] Syscall 4 (malloc 128B) .... ", 0
t_free:    db "[TEST] Syscall 5 (free) ........... ", 0
t_malloc2: db "[TEST] Syscall 4 (malloc 64KB) .... ", 0
all_done:  db "[TEST] All tests passed!", 0xD, 0xA, 0
ok:        db "OK", 0xD, 0xA, 0
