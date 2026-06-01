BITS 64

SYSCALL_EXIT        equ 1
SYSCALL_PRINT       equ 3
SYSCALL_OPEN        equ 6
SYSCALL_READ        equ 7
SYSCALL_WRITE       equ 8
SYSCALL_CLOSE       equ 9
SYSCALL_GETCHAR     equ 10
SYSCALL_READDIR     equ 16
SYSCALL_PE_CHECK    equ 17
SYSCALL_EXEC_WAIT   equ 18
SYSCALL_EXISTS      equ 19
SYSCALL_GUI_CREATE  equ 20
SYSCALL_GUI_PUTS    equ 22
SYSCALL_GUI_PUTCHAR equ 23
SYSCALL_GUI_CLEAR   equ 24
SYSCALL_GUI_SET_TITLE equ 25
SYSCALL_GUI_GET_RECT equ 26
SYSCALL_GUI_GET_EVENT equ 27
SYSCALL_YIELD       equ 28

PATH_MAX       equ 256
TEMP_BUF_SIZE  equ 4096
FONT_HEIGHT    equ 16
GUI_TITLE_HEIGHT equ 18
MAX_ENTRIES    equ 256

section .text

start:
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8

    lea rdi, [rel current_dir]
    mov byte [rdi], '\'
    mov byte [rdi + 1], 0

    mov rax, SYSCALL_GUI_CREATE
    lea rdi, [rel title_str]
    mov esi, 60
    mov edx, 40
    mov r10d, 640
    mov r8d,  420
    syscall
    mov [rel scout_win], eax
    test eax, eax
    js .exit

    lea rdi, [rel current_dir]
    call load_dir
    call refresh

.main_loop:
    mov rax, SYSCALL_GUI_GET_EVENT
    mov edi, [rel scout_win]
    lea rsi, [rel event_buf]
    syscall
    test eax, eax
    jnz .have_event
    jmp .yield

.have_event:
    cmp dword [rel event_buf], 1
    jne .yield

    mov r12d, [rel event_buf + 4]
    mov r13d, [rel event_buf + 8]

    mov rax, SYSCALL_GUI_GET_RECT
    mov edi, [rel scout_win]
    lea rsi, [rel win_rect]
    syscall

    mov eax, r13d
    sub eax, [rel win_rect + 4]
    sub eax, GUI_TITLE_HEIGHT + 1
    js .yield

    xor edx, edx
    mov ecx, FONT_HEIGHT
    div ecx
    mov r14d, eax

    cmp r14d, 2
    jb .yield

    sub r14d, 2
    cmp r14d, [rel num_entries]
    jae .yield

    lea r15, [rel entries_buf]
    xor ebx, ebx

.find_entry:
    cmp ebx, r14d
    je .found_entry
    inc r15
    inc r15
    mov rdi, r15
    push rbx
    push r14
    call strlen
    pop r14
    pop rbx
    add r15, rax
    inc r15
    mov rdi, r15
    push rbx
    push r14
    call strlen
    pop r14
    pop rbx
    add r15, rax
    inc r15
    inc ebx
    jmp .find_entry

.found_entry:
    mov al, [r15]
    mov [rel entry_type], al
    lea rsi, [r15 + 2]
    lea rdi, [rel entry_name_buf]
    call strcpy

    cmp byte [rel entry_type], 'D'
    je .handle_dir

    lea rdi, [rel entry_name_buf]
    call has_exe_ext
    test rax, rax
    jnz .handle_exe
    jmp .yield

.handle_dir:
    lea rdi, [rel entry_name_buf]
    cmp byte [rdi], '.'
    jne .nav_normal
    cmp byte [rdi + 1], 0
    je .yield
    cmp byte [rdi + 1], '.'
    jne .nav_normal
    cmp byte [rdi + 2], 0
    jne .nav_normal
    call go_up
    call load_dir
    call refresh
    jmp .yield

.nav_normal:
    lea rdi, [rel new_path]
    lea rsi, [rel current_dir]
    call strcpy
    lea rdi, [rel new_path]
    call strlen
    lea rdi, [rel new_path]
    add rdi, rax
    cmp rax, 1
    je .nav_add_name
    cmp byte [rdi - 1], '\'
    je .nav_add_name
    mov byte [rdi], '\'
    inc rdi
.nav_add_name:
    lea rsi, [rel entry_name_buf]
    call strcpy
    mov rax, SYSCALL_EXISTS
    lea rdi, [rel new_path]
    syscall
    test rax, rax
    jz .yield
    lea rsi, [rel new_path]
    lea rdi, [rel current_dir]
    call strcpy
    call load_dir
    call refresh
    jmp .yield

.handle_exe:
    lea rdi, [rel new_path]
    lea rsi, [rel current_dir]
    call strcpy
    lea rdi, [rel new_path]
    call strlen
    lea rdi, [rel new_path]
    add rdi, rax
    cmp rax, 1
    je .exe_add_name
    cmp byte [rdi - 1], '\'
    je .exe_add_name
    mov byte [rdi], '\'
    inc rdi
.exe_add_name:
    lea rsi, [rel entry_name_buf]
    call strcpy
    mov edi, [rel scout_win]
    lea rsi, [rel launch_msg]
    mov rax, SYSCALL_GUI_PUTS
    syscall
    mov edi, [rel scout_win]
    lea rsi, [rel entry_name_buf]
    mov rax, SYSCALL_GUI_PUTS
    syscall
    mov edi, [rel scout_win]
    lea rsi, [rel dots_nl]
    mov rax, SYSCALL_GUI_PUTS
    syscall
    lea rdi, [rel new_path]
    mov rax, SYSCALL_EXEC_WAIT
    syscall
    lea rdi, [rel current_dir]
    call load_dir
    call refresh
    jmp .yield

.yield:
    mov rax, SYSCALL_YIELD
    syscall
    jmp .main_loop

.exit:
    xor edi, edi
    mov rax, SYSCALL_EXIT
    syscall

; ================ helpers ================

strlen:
    push rdi
    xor eax, eax
.loop:
    cmp byte [rdi + rax], 0
    je .done
    inc rax
    jmp .loop
.done:
    pop rdi
    ret

strcpy:
    push rax
    push rcx
    xor ecx, ecx
.loop:
    mov al, [rsi + rcx]
    mov [rdi + rcx], al
    inc rcx
    test al, al
    jnz .loop
    pop rcx
    pop rax
    ret

has_exe_ext:
    push rdi
    push rcx
    call strlen
    cmp rax, 5
    jb .no_exe
    add rdi, rax
    sub rdi, 4
    cmp byte [rdi], '.'
    jne .no_exe
    mov al, [rdi + 1]
    or al, 32
    cmp al, 'e'
    jne .no_exe
    mov al, [rdi + 2]
    or al, 32
    cmp al, 'x'
    jne .no_exe
    mov al, [rdi + 3]
    or al, 32
    cmp al, 'e'
    jne .no_exe
    mov rax, 1
    pop rcx
    pop rdi
    ret
.no_exe:
    xor eax, eax
    pop rcx
    pop rdi
    ret

go_up:
    push rdi
    push rcx
    lea rdi, [rel current_dir]
    call strlen
    cmp rax, 1
    jbe .root
    mov rcx, rax
    dec rcx
.loop:
    cmp byte [rdi + rcx], '\'
    je .found
    dec rcx
    jns .loop
.root:
    mov byte [rdi], '\'
    mov byte [rdi + 1], 0
    pop rcx
    pop rdi
    ret
.found:
    cmp rcx, 0
    je .root
    mov byte [rdi + rcx], 0
    pop rcx
    pop rdi
    ret

load_dir:
    push rax
    push rdi
    push rsi
    push rdx
    push r15
    push r14
    push rbx

    mov rax, SYSCALL_READDIR
    lea rdi, [rel current_dir]
    lea rsi, [rel entries_buf]
    mov edx, TEMP_BUF_SIZE
    syscall

    mov dword [rel num_entries], 0
    cmp eax, 0
    jle .done

    mov r14d, eax
    lea r15, [rel entries_buf]
    xor ebx, ebx

.count:
    mov rax, r15
    lea rcx, [rel entries_buf]
    sub rax, rcx
    cmp eax, r14d
    jge .count_done
    cmp byte [r15], 0
    je .count_done

    inc ebx
    inc r15
    inc r15
    mov rdi, r15
    push rbx
    push r14
    call strlen
    pop r14
    pop rbx
    add r15, rax
    inc r15
    mov rdi, r15
    push rbx
    push r14
    call strlen
    pop r14
    pop rbx
    add r15, rax
    inc r15
    jmp .count

.count_done:
    mov [rel num_entries], ebx

.done:
    pop rbx
    pop r14
    pop r15
    pop rdx
    pop rsi
    pop rdi
    pop rax
    ret

refresh:
    push rax
    push rdi
    push rsi
    push rbx
    push r15
    push rcx

    mov edi, [rel scout_win]
    mov rax, SYSCALL_GUI_CLEAR
    syscall

    mov edi, [rel scout_win]
    lea rsi, [rel path_prefix]
    mov rax, SYSCALL_GUI_PUTS
    syscall

    lea rsi, [rel current_dir]
    cmp byte [rsi], '\'
    jne .print_path
    inc rsi
.print_path:
    mov edi, [rel scout_win]
    mov rax, SYSCALL_GUI_PUTS
    syscall

    mov edi, [rel scout_win]
    mov esi, 10
    mov rax, SYSCALL_GUI_PUTCHAR
    syscall

    mov edi, [rel scout_win]
    mov esi, 10
    mov rax, SYSCALL_GUI_PUTCHAR
    syscall

    lea rdi, [rel title_buf]
    lea rsi, [rel title_prefix]
    call strcpy
    lea rdi, [rel title_buf]
    call strlen
    lea rdi, [rel title_buf]
    add rdi, rax
    lea rsi, [rel current_dir]
    cmp byte [rsi], '\'
    jne .title_do
    inc rsi
.title_do:
    call strcpy

    mov edi, [rel scout_win]
    lea rsi, [rel title_buf]
    mov rax, SYSCALL_GUI_SET_TITLE
    syscall

    lea r15, [rel entries_buf]
    xor ebx, ebx

.entry_loop:
    cmp ebx, [rel num_entries]
    jae .refresh_done
    cmp byte [r15], 0
    je .refresh_done

    mov al, [r15]
    cmp al, 'D'
    je .is_dir

    lea rdi, [rel line_buf]
    xor ecx, ecx
    lea rsi, [r15 + 2]
.copy_name:
    mov al, [rsi]
    test al, al
    jz .name_done
    cmp cl, 62
    jae .name_done
    mov [rdi + rcx], al
    inc rsi
    inc ecx
    jmp .copy_name
.name_done:
.pad_loop:
    cmp cl, 50
    jae .pad_done
    mov byte [rdi + rcx], ' '
    inc ecx
    jmp .pad_loop
.pad_done:

    lea rsi, [r15 + 2]
    push rcx
    call strlen
    pop rcx
    add rsi, rax
    inc rsi
.size_copy:
    mov al, [rsi]
    test al, al
    jz .size_done
    cmp cl, 126
    jae .size_done
    mov [rdi + rcx], al
    inc rsi
    inc ecx
    jmp .size_copy
.size_done:
    mov byte [rdi + rcx], 0
    jmp .print_line

.is_dir:
    lea rdi, [rel line_buf]
    mov byte [rdi], '['
    mov ecx, 1
    lea rsi, [r15 + 2]
.copy_dir:
    mov al, [rsi]
    test al, al
    jz .dir_done
    cmp cl, 62
    jae .dir_done
    mov [rdi + rcx], al
    inc rsi
    inc ecx
    jmp .copy_dir
.dir_done:
    cmp cl, 63
    jae .close_b
    mov byte [rdi + rcx], ']'
    inc ecx
.close_b:
    mov byte [rdi + rcx], 0

.print_line:
    mov edi, [rel scout_win]
    lea rsi, [rel line_buf]
    mov rax, SYSCALL_GUI_PUTS
    syscall

    mov edi, [rel scout_win]
    mov esi, 10
    mov rax, SYSCALL_GUI_PUTCHAR
    syscall

    inc r15
    inc r15
    mov rdi, r15
    push rbx
    call strlen
    pop rbx
    add r15, rax
    inc r15
    mov rdi, r15
    push rbx
    call strlen
    pop rbx
    add r15, rax
    inc r15

    inc ebx
    jmp .entry_loop

.refresh_done:
    pop rcx
    pop r15
    pop rbx
    pop rsi
    pop rdi
    pop rax
    ret

; ================ data ================
align 8

title_str:     db "Scout - File Browser", 0
title_prefix:  db "Scout - ", 0
path_prefix:   db "\", 0
launch_msg:    db 13, 10, "Launching ", 0
dots_nl:       db "...", 13, 10, 0

align 4
scout_win:     dd 0
num_entries:   dd 0
entry_type:    db 0

align 8
event_buf:     times 16 db 0
win_rect:      times 16 db 0
entry_name_buf: times 64 db 0
current_dir:   times PATH_MAX db 0
new_path:      times PATH_MAX db 0
entries_buf:   times TEMP_BUF_SIZE db 0
line_buf:      times 128 db 0
title_buf:     times 64 db 0
