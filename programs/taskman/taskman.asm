BITS 64

SYSCALL_EXIT       equ 1
SYSCALL_PUTS       equ 3
SYSCALL_SLEEP      equ 13
SYSCALL_GUI_CREATE equ 20
SYSCALL_GUI_EVENT  equ 27
SYSCALL_YIELD      equ 28
SYSCALL_GUI_RENDER equ 29
SYSCALL_GUI_DRAW   equ 30
SYSCALL_GUI_TEXT   equ 31
SYSCALL_PROC_COUNT equ 34
SYSCALL_PROC_INFO  equ 35

WIN_W              equ 580
WIN_H              equ 420
ROW_H              equ 18
HEADER_H           equ 24
COL_PID            equ 8
COL_NAME           equ 80
COL_STATE          equ 400

section .text
start:
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov rax, SYSCALL_GUI_CREATE
    lea rdi, [rel title_str]
    mov r10, WIN_W
    mov r8, WIN_H
    syscall
    mov [rel win_id], eax
    test eax, eax
    js exit

main_loop:
    mov rax, SYSCALL_GUI_EVENT
    mov rdi, [rel win_id]
    lea r10, [rel event_buf]
    syscall
    test rax, rax
    jz .render
    cmp byte [rel event_buf], 0
    jne exit

.render:
    mov rax, SYSCALL_GUI_DRAW
    mov rdi, [rel win_id]
    xor rsi, rsi
    xor rdx, rdx
    mov r10, WIN_W
    mov r8, WIN_H
    mov r9, 0xF0F0F0
    syscall

    mov rax, SYSCALL_GUI_DRAW
    mov rdi, [rel win_id]
    xor rsi, rsi
    xor rdx, rdx
    mov r10, WIN_W
    mov r8, HEADER_H
    mov r9, 0x0078D7
    syscall

    mov rax, SYSCALL_GUI_TEXT
    mov rdi, [rel win_id]
    mov rsi, COL_PID
    mov rdx, 5
    lea r10, [rel hdr_pid_str]
    mov r8, 0xFFFFFF
    mov r9, 0x0078D7
    syscall

    mov rax, SYSCALL_GUI_TEXT
    mov rdi, [rel win_id]
    mov rsi, COL_NAME
    mov rdx, 5
    lea r10, [rel hdr_name_str]
    mov r8, 0xFFFFFF
    mov r9, 0x0078D7
    syscall

    mov rax, SYSCALL_GUI_TEXT
    mov rdi, [rel win_id]
    mov rsi, COL_STATE
    mov rdx, 5
    lea r10, [rel hdr_state_str]
    mov r8, 0xFFFFFF
    mov r9, 0x0078D7
    syscall

    mov rax, SYSCALL_PROC_COUNT
    syscall
    mov [rel proc_count], eax
    xor r15d, r15d
    cmp eax, 0
    jle .done

.proc_loop:
    mov rax, SYSCALL_PROC_INFO
    mov rdi, r15
    lea rsi, [rel pid_val]
    lea rdx, [rel name_val]
    lea r10, [rel state_val]
    syscall
    test rax, rax
    js .next

    mov eax, r15d
    and eax, 1
    jnz .alt_row
    mov r9d, 0xFFFFFF
    jmp .row_bg
.alt_row:
    mov r9d, 0xE8F0F8
.row_bg:
    mov rax, SYSCALL_GUI_DRAW
    mov rdi, [rel win_id]
    xor rsi, rsi
    mov edx, r15d
    imul edx, ROW_H
    add edx, HEADER_H
    mov r10, WIN_W
    mov r8, ROW_H
    syscall

    mov eax, [rel pid_val]
    lea rdi, [rel pid_str]
    call itoa

    mov rax, SYSCALL_GUI_TEXT
    mov rdi, [rel win_id]
    mov rsi, COL_PID
    mov edx, r15d
    imul edx, ROW_H
    add edx, HEADER_H
    add edx, 2
    lea r10, [rel pid_str]
    mov r8, 0x000000
    mov r9d, r15d
    and r9d, 1
    jnz .alt_bg1
    mov r9d, 0xFFFFFF
    jmp .draw_name
.alt_bg1:
    mov r9d, 0xE8F0F8
.draw_name:
    syscall

    mov rax, SYSCALL_GUI_TEXT
    mov rdi, [rel win_id]
    mov rsi, COL_NAME
    mov edx, r15d
    imul edx, ROW_H
    add edx, HEADER_H
    add edx, 2
    lea r10, [rel name_val]
    mov r8, 0x000000
    mov r9d, r15d
    and r9d, 1
    jnz .alt_bg2
    mov r9d, 0xFFFFFF
    jmp .draw_state
.alt_bg2:
    mov r9d, 0xE8F0F8
.draw_state:
    syscall

    mov eax, [rel state_val]
    cmp eax, 0
    je .st_created
    cmp eax, 1
    je .st_ready
    cmp eax, 2
    je .st_running
    cmp eax, 3
    je .st_blocked
    cmp eax, 4
    je .st_term
    lea r14, [rel state_unknown]
    jmp .draw_state_text
.st_created:
    lea r14, [rel state_created]
    jmp .draw_state_text
.st_ready:
    lea r14, [rel state_ready]
    jmp .draw_state_text
.st_running:
    lea r14, [rel state_running]
    jmp .draw_state_text
.st_blocked:
    lea r14, [rel state_blocked]
    jmp .draw_state_text
.st_term:
    lea r14, [rel state_term]
.draw_state_text:
    mov rax, SYSCALL_GUI_TEXT
    mov rdi, [rel win_id]
    mov rsi, COL_STATE
    mov edx, r15d
    imul edx, ROW_H
    add edx, HEADER_H
    add edx, 2
    mov r10, r14
    mov r8, 0x000000
    mov r9d, r15d
    and r9d, 1
    jnz .alt_bg3
    mov r9d, 0xFFFFFF
    jmp .draw_st
.alt_bg3:
    mov r9d, 0xE8F0F8
.draw_st:
    syscall

.next:
    inc r15d
    cmp r15d, [rel proc_count]
    jl .proc_loop

.done:
    mov rax, SYSCALL_GUI_RENDER
    syscall

    mov rax, SYSCALL_SLEEP
    mov rdi, 1000
    syscall

    mov rax, SYSCALL_YIELD
    syscall
    jmp main_loop

exit:
    xor rdi, rdi
    mov rax, SYSCALL_EXIT
    syscall

itoa:
    push rbx
    push rcx
    push rdx
    push rdi
    mov rcx, 10
    mov rbx, rdi
    add rbx, 11
    mov byte [rbx], 0
    dec rbx
    test rax, rax
    jnz .loop
    mov byte [rbx], '0'
    dec rbx
    jmp .done
.loop:
    xor rdx, rdx
    div rcx
    add dl, '0'
    mov byte [rbx], dl
    dec rbx
    test rax, rax
    jnz .loop
.done:
    inc rbx
    mov rdi, rbx
    pop rdi
    mov rax, rbx
    pop rdx
    pop rcx
    pop rbx
    ret

title_str:      db "Task Manager", 0
hdr_pid_str:    db "PID", 0
hdr_name_str:   db "Process Name", 0
hdr_state_str:  db "State", 0
state_created:  db "Created", 0
state_ready:    db "Ready", 0
state_running:  db "Running", 0
state_blocked:  db "Blocked", 0
state_term:     db "Terminated", 0
state_unknown:  db "Unknown", 0

win_id:         dd 0
proc_count:     dd 0
pid_val:        dd 0
state_val:      dd 0
name_val:       times 32 db 0
pid_str:        times 12 db 0
event_buf:      times 16 db 0
