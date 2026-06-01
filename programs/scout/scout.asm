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
SYSCALL_DRAW_RECT   equ 30
SYSCALL_DRAW_TEXT   equ 31
SYSCALL_GUI_RENDER  equ 29

PATH_MAX       equ 256
TEMP_BUF_SIZE  equ 4096
FONT_HEIGHT    equ 16
FONT_WIDTH     equ 8
GUI_TITLE_HEIGHT equ 18
ROW_HEIGHT     equ 20
MAX_ENTRIES    equ 256

COL_WIN_BLUE2   equ 0x000000AA
COL_WHITE       equ 0x00FFFFFF
COL_BLACK       equ 0x00000000
COL_LIGHT_GRAY  equ 0x00C0C0C0
COL_DARK_GRAY   equ 0x00808080
COL_FOLDER      equ 0x00408040
COL_EXE_GREEN   equ 0x0000AA00
COL_FILE_GRAY   equ 0x00A0A0A0
COL_HEADER_BG   equ 0x00000080
COL_EVEN_ROW    equ 0x00F0F0F0
COL_SELECTED    equ 0x003366FF

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

    sub eax, 22
    js .yield

    xor edx, edx
    mov ecx, ROW_HEIGHT
    div ecx
    mov r14d, eax

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

; ================ graphical refresh ================

refresh:
    push rax
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push r15
    push r8
    push r9
    push r10

    mov edi, [rel scout_win]
    mov rax, SYSCALL_GUI_CLEAR
    syscall

    ; Draw header bar
    mov edi, [rel scout_win]
    xor esi, esi
    xor edx, edx
    mov r10d, 638
    mov r8d, 20
    mov r9d, COL_HEADER_BG
    mov rax, SYSCALL_DRAW_RECT
    syscall

    ; Draw path text in header
    lea rsi, [rel current_dir]
    cmp byte [rsi], '\'
    jne .print_path
    inc rsi
.print_path:
    mov edi, [rel scout_win]
    mov esi, 5
    mov edx, 2
    lea rcx, [rel current_dir]
    ; Check if current_dir starts with \
    lea r10, [rel current_dir]
    cmp byte [r10], '\'
    jne .path_go
    lea r10, [rel current_dir]
    inc r10
.path_go:
    mov r8d, COL_WHITE
    mov r9d, COL_HEADER_BG
    mov rax, SYSCALL_DRAW_TEXT
    syscall

    ; Update title
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

    ; Draw entries
    lea r15, [rel entries_buf]
    xor ebx, ebx

.entry_loop:
    cmp ebx, [rel num_entries]
    jae .entry_done
    cmp byte [r15], 0
    je .entry_done

    mov eax, ebx
    mov ecx, ROW_HEIGHT
    mul ecx
    add eax, 22
    mov r14d, eax  ; r14 = row_y (pixel y of this entry)

    ; Draw alternating row background
    test bl, 1
    jnz .skip_row_bg
    mov edi, [rel scout_win]
    mov esi, 2
    mov edx, r14d
    mov r10d, 636
    mov r8d, ROW_HEIGHT
    mov r9d, COL_EVEN_ROW
    mov rax, SYSCALL_DRAW_RECT
    syscall
.skip_row_bg:

    ; Draw icon
    mov al, [r15]
    cmp al, 'D'
    je .icon_dir
    cmp al, 'F'
    je .icon_file

    ; Determine if file is .exe
    push r15
    push rbx
    mov rdi, r15
    add rdi, 2
    call has_exe_ext
    pop rbx
    pop r15
    test rax, rax
    jnz .icon_exe
    jmp .icon_file

.icon_dir:
    mov r9d, COL_FOLDER
    jmp .draw_icon
.icon_exe:
    mov r9d, COL_EXE_GREEN
    jmp .draw_icon
.icon_file:
    mov r9d, COL_FILE_GRAY
.draw_icon:
    push r15
    push rbx
    mov edi, [rel scout_win]
    mov esi, 4
    lea edx, [r14d + 4]
    mov r10d, ICON_SIZE
    mov r8d, ICON_SIZE
    mov rax, SYSCALL_DRAW_RECT
    syscall
    pop rbx
    pop r15

    ; Draw name text
    lea r10, [r15 + 2]
    push r15
    push rbx
    mov edi, [rel scout_win]
    mov esi, 22
    lea edx, [r14d + 2]
    mov r8d, COL_BLACK
    mov r9d, COL_WHITE
    mov rax, SYSCALL_DRAW_TEXT
    syscall
    pop rbx
    pop r15

    ; Draw size text
    push r15
    push rbx
    lea rdi, [r15 + 2]
    call strlen
    lea r10, [r15 + 2]
    add r10, rax
    inc r10
    mov edi, [rel scout_win]
    mov esi, 450
    lea edx, [r14d + 2]
    mov r8d, COL_DARK_GRAY
    mov r9d, COL_WHITE
    mov rax, SYSCALL_DRAW_TEXT
    syscall
    pop rbx
    pop r15

    ; Advance to next entry
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

.entry_done:
    mov rax, SYSCALL_GUI_RENDER
    syscall
    pop r10
    pop r9
    pop r8
    pop r15
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rax
    ret

; ================ data ================
ICON_SIZE      equ 12

title_str:     db "Scout - File Browser", 0
title_prefix:  db "Scout - ", 0

scout_win:     dd 0
num_entries:   dd 0
entry_type:    db 0

event_buf:     times 16 db 0
win_rect:      times 16 db 0
entry_name_buf: times 64 db 0
current_dir:   times PATH_MAX db 0
new_path:      times PATH_MAX db 0
entries_buf:   times TEMP_BUF_SIZE db 0
title_buf:     times 64 db 0
