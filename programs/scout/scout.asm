BITS 64

SYSCALL_READDIR      equ 16
SYSCALL_PE_CHECK     equ 17
SYSCALL_EXEC_WAIT    equ 18
SYSCALL_GUI_CREATE   equ 20
SYSCALL_GUI_GET_WIN_RECT equ 26
SYSCALL_GUI_GET_EVENT equ 27
SYSCALL_YIELD        equ 28
SYSCALL_GUI_RENDER   equ 29
SYSCALL_GUI_DRAW_RECT equ 30
SYSCALL_GUI_DRAW_TEXT equ 31

FONT_HEIGHT equ 16
FONT_WIDTH  equ 8
GUI_TITLE_HEIGHT equ 18
WIN_W equ 660
WIN_H equ 420
ENTRY_START_Y equ 38
MAX_VISIBLE equ 22

COL_WHITE    equ 0x00FFFFFF
COL_BLACK    equ 0x00000000
COL_PATH_BG  equ 0x00FFFFFF
COL_PATH_FG  equ 0x00000050
COL_HEAD_BG  equ 0x00E8E8E8
COL_SEP      equ 0x00808080
COL_DIR_FG   equ 0x00000080
COL_DIR_BG   equ 0x00E8F0FE
COL_FILE_FG  equ 0x00000000
COL_FILE_BG  equ 0x00FFFFFF
COL_HILITE   equ 0x00316AC5
COL_BTN_BG   equ 0x00ECE9D8
COL_UP_BG    equ 0x00ECE9D8
COL_UP_FG    equ 0x00000000
COL_STATUS   equ 0x00C0C0C0

section .text

start:
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov byte [rel current_dir], '\'
    mov byte [rel current_dir + 1], 0

    mov rax, SYSCALL_GUI_CREATE
    lea rdi, [rel title_str]
    mov r10, WIN_W
    mov r8, WIN_H
    syscall
    mov [rel win_id], eax

main_loop:
    call read_current_dir
    mov [rel entry_count], eax

    call draw_all

    mov rax, SYSCALL_GUI_RENDER
    syscall

    mov rax, SYSCALL_GUI_GET_EVENT
    mov edi, [rel win_id]
    lea rsi, [rel event_buf]
    syscall
    test eax, eax
    jz .no_event
    call handle_event

.no_event:
    mov rax, SYSCALL_YIELD
    syscall
    jmp main_loop

read_current_dir:
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    mov rax, SYSCALL_READDIR
    lea rdi, [rel current_dir]
    lea rsi, [rel entries_buf]
    mov rdx, 4096
    syscall
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    ret

draw_all:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10

    mov eax, [rel entry_count]
    test eax, eax
    jle .draw_all_done

    xor r14d, r14d
    lea r14, [rel entries_buf]

    call draw_path_bar
    call draw_header

    xor r12d, r12d
    xor r13d, r13d
    xor r15d, r15d

.draw_entry_loop:
    mov al, [r14]
    test al, al
    jz .draw_done
    cmp al, 'D'
    je .is_dir

; File entry
    cmp r15d, MAX_VISIBLE
    jae .skip_entry
    push r14
    call draw_file_entry
    pop r14
    inc r12d
    inc r15d
    jmp .skip_entry

.is_dir:
    push r14
    lea rdi, [r14 + 2]
    call strlen
    pop r14
    cmp rax, 1
    jne .not_dot
    push r14
    lea rdi, [r14 + 2]
    cmp byte [rdi], '.'
    je .skip_entry

.not_dot:
    inc r13d
    cmp r15d, MAX_VISIBLE
    jae .skip_entry
    push r14
    call draw_dir_entry
    pop r14
    inc r15d

.skip_entry:
    push rcx
    push rbx
    lea rdi, [r14 + 2]
    call strlen
    mov rcx, rax
    lea rdi, [r14 + 2 + rcx + 1]
    call strlen
    mov rbx, rax
    mov r8, rcx
    add r8, rbx
    add r8, 4
    add r14, r8
    pop rbx
    pop rcx
    jmp .draw_entry_loop

.draw_done:
    call draw_status_bar

.draw_all_done:
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

draw_path_bar:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10

    mov eax, [rel win_id]
    mov edi, eax
    xor esi, esi
    xor edx, edx
    mov r10d, WIN_W
    mov r8d, 18
    mov r9d, COL_PATH_BG
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    mov edi, [rel win_id]
    xor esi, esi
    mov edx, 18
    mov r10d, WIN_W
    mov r8d, 1
    mov r9d, COL_SEP
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    lea rdi, [rel path_display_buf]
    mov byte [rdi], ' '
    mov byte [rdi + 1], ' '
    lea rsi, [rel current_dir]
    call strcpy
    mov edi, [rel win_id]
    xor esi, esi
    xor edx, edx
    lea r10, [rel path_display_buf]
    mov r8d, COL_PATH_FG
    mov r9d, COL_PATH_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

draw_header:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10

    mov edi, [rel win_id]
    xor esi, esi
    mov edx, 20
    mov r10d, WIN_W
    mov r8d, 16
    mov r9d, COL_HEAD_BG
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    mov edi, [rel win_id]
    xor esi, esi
    mov edx, 20
    lea r10, [rel header_str]
    mov r8d, COL_BLACK
    mov r9d, COL_HEAD_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

draw_dir_entry:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r14
    push r15

    mov r14, [rsp + 8]

    mov eax, [rel win_id]
    mov edi, eax
    xor esi, esi
    mov edx, ENTRY_START_Y
    imul edx, r15d, FONT_HEIGHT
    add edx, ENTRY_START_Y
    mov r10d, WIN_W
    mov r8d, FONT_HEIGHT
    mov r9d, COL_DIR_BG
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    lea rdi, [rel line_buf]
    mov word [rdi], '  '
    lea rsi, [r14 + 2]
    lea rdi, [rdi + 2]
    call strcpy
    lea rdi, [rel line_buf]
    call strlen
    lea rdi, [rel line_buf]
    add rdi, rax
    mov byte [rdi], '\'

    mov edi, [rel win_id]
    xor esi, esi
    mov edx, ENTRY_START_Y
    imul edx, r15d, FONT_HEIGHT
    add edx, ENTRY_START_Y
    lea r10, [rel line_buf]
    mov r8d, COL_DIR_FG
    mov r9d, COL_DIR_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    pop r15
    pop r14
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

draw_file_entry:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r14
    push r15

    mov r14, [rsp + 8]

    mov eax, [rel win_id]
    mov edi, eax
    xor esi, esi
    mov edx, ENTRY_START_Y
    imul edx, r15d, FONT_HEIGHT
    add edx, ENTRY_START_Y
    mov r10d, WIN_W
    mov r8d, FONT_HEIGHT
    mov r9d, COL_FILE_BG
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    lea rdi, [rel line_buf]
    mov word [rdi], '  '
    lea rsi, [r14 + 2]
    lea rdi, [rdi + 2]
    call strcpy

    lea rdi, [r14 + 2]
    call strlen
    lea rdi, [r14 + 2 + rax + 1]
    mov byte [rel size_buf], 0
    cmp byte [rdi], 0
    je .file_no_size
    lea rsi, [rel size_buf]
    call strcpy
.file_no_size:

    mov edi, [rel win_id]
    xor esi, esi
    mov edx, ENTRY_START_Y
    imul edx, r15d, FONT_HEIGHT
    add edx, ENTRY_START_Y
    lea r10, [rel line_buf]
    mov r8d, COL_FILE_FG
    mov r9d, COL_FILE_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    lea rdi, [rel size_buf]
    cmp byte [rdi], 0
    je .file_no_draw_size
    mov edi, [rel win_id]
    mov esi, 250
    mov edx, ENTRY_START_Y
    imul edx, r15d, FONT_HEIGHT
    add edx, ENTRY_START_Y
    lea r10, [rel size_buf]
    mov r8d, COL_BLACK
    mov r9d, COL_FILE_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall
.file_no_draw_size:

    pop r15
    pop r14
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

draw_status_bar:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10

    mov edi, [rel win_id]
    xor esi, esi
    mov edx, WIN_H
    sub edx, GUI_TITLE_HEIGHT
    sub edx, 18
    mov r10d, WIN_W
    mov r8d, 18
    mov r9d, COL_STATUS
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

handle_event:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10

    mov rax, SYSCALL_GUI_GET_WIN_RECT
    mov edi, [rel win_id]
    lea rsi, [rel win_rect]
    syscall

    mov eax, [rel event_buf + 4]
    sub eax, [rel win_rect]
    sub eax, 1
    mov [rel rel_x], eax

    mov eax, [rel event_buf + 8]
    sub eax, [rel win_rect + 4]
    sub eax, GUI_TITLE_HEIGHT
    sub eax, 1
    mov [rel rel_y], eax

    mov eax, [rel rel_y]
    cmp eax, ENTRY_START_Y
    jl .event_done

    sub eax, ENTRY_START_Y
    xor edx, edx
    mov ecx, FONT_HEIGHT
    div ecx
    mov [rel click_entry], eax

    xor r14d, r14d
    lea r14, [rel entries_buf]
    xor r15d, r15d

.find_entry:
    mov al, [r14]
    test al, al
    jz .event_done

    cmp al, 'D'
    je .found_dir

; File entry
    cmp r15d, [rel click_entry]
    jne .not_match_file
    push r14
    call exec_entry
    pop r14
    jmp .event_done
.not_match_file:
    inc r15d
    jmp .advance

.found_dir:
    push r14
    lea rdi, [r14 + 2]
    call strlen
    pop r14
    cmp rax, 1
    jne .check_dir_click
    push r14
    lea rdi, [r14 + 2]
    cmp byte [rdi], '.'
    je .advance

.check_dir_click:
    cmp r15d, [rel click_entry]
    jne .not_match_dir
    push r14
    call navigate_dir
    pop r14
    jmp .event_done
.not_match_dir:
    inc r15d

.advance:
    push rcx
    push rbx
    lea rdi, [r14 + 2]
    call strlen
    mov rcx, rax
    lea rdi, [r14 + 2 + rcx + 1]
    call strlen
    mov rbx, rax
    mov r8, rcx
    add r8, rbx
    add r8, 4
    add r14, r8
    pop rbx
    pop rcx
    jmp .find_entry

.event_done:
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

navigate_dir:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi

    lea rdi, [r14 + 2]
    cmp byte [rdi], '.'
    jne .not_parent
    cmp byte [rdi + 1], '.'
    jne .not_parent

    call go_up_dir
    jmp .nav_done

.not_parent:
    lea rsi, [rel current_dir]
    lea rdi, [rel path_buf]
    call strcpy

    lea rdi, [rel path_buf]
    call strlen
    add rdi, rax

    lea rsi, [r14 + 2]
    call strcpy

    lea rdi, [rel path_buf]
    call strlen
    lea rdi, [rel path_buf]
    add rdi, rax
    cmp byte [rdi - 1], '\'
    je .nav_got_slash
    mov byte [rdi], '\'
    mov byte [rdi + 1], 0

.nav_got_slash:
    lea rsi, [rel path_buf]
    lea rdi, [rel current_dir]
    call strcpy

.nav_done:
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

go_up_dir:
    push rax
    push rbx
    push rcx
    push rdi

    lea rdi, [rel current_dir]
    call strlen
    mov ecx, eax
    cmp ecx, 1
    jle .up_done

    mov edi, ecx
    sub edi, 2

.up_loop:
    cmp edi, 0
    jle .up_to_root
    lea rax, [rel current_dir]
    cmp byte [rax + rdi], '\'
    je .up_found
    dec edi
    jmp .up_loop

.up_to_root:
    lea rdi, [rel current_dir]
    mov byte [rdi], '\'
    mov byte [rdi + 1], 0
    jmp .up_done

.up_found:
    lea rax, [rel current_dir]
    cmp edi, 0
    je .up_to_root
    mov byte [rax + rdi + 1], 0

.up_done:
    pop rdi
    pop rcx
    pop rbx
    pop rax
    ret

exec_entry:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi

    lea rsi, [rel current_dir]
    lea rdi, [rel path_buf]
    call strcpy

    lea rdi, [rel path_buf]
    call strlen
    add rdi, rax

    lea rsi, [r14 + 2]
    call strcpy

    mov rax, SYSCALL_EXEC_WAIT
    lea rdi, [rel path_buf]
    syscall

    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

strlen:
    push rdi
    xor eax, eax
.l:
    cmp byte [rdi], 0
    je .d
    inc rdi
    inc eax
    jmp .l
.d:
    pop rdi
    ret

strcpy:
    push rax
    push rcx
    push rsi
    push rdi
    xor ecx, ecx
.l:
    mov al, [rsi + rcx]
    mov [rdi + rcx], al
    inc rcx
    test al, al
    jnz .l
    pop rdi
    pop rsi
    pop rcx
    pop rax
    ret

title_str:      db "Scout File Browser", 0
header_str:     db "  Name", 0

align 8
win_id:             dd 0
current_dir:        times 256 db 0
path_buf:           times 256 db 0
entries_buf:        times 4096 db 0
path_display_buf:   times 260 db 0
win_rect:           times 16 db 0
event_buf:          times 16 db 0
entry_count:        dd 0
rel_x:              dd 0
rel_y:              dd 0
click_entry:        dd 0
line_buf:           times 128 db 0
size_buf:           times 32 db 0
