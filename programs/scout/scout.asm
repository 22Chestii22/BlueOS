BITS 64

SYSCALL_READDIR        equ 16
SYSCALL_PE_CHECK       equ 17
SYSCALL_EXEC_WAIT      equ 18
SYSCALL_GUI_CREATE     equ 20
SYSCALL_GUI_GET_WIN_RECT equ 26
SYSCALL_GUI_GET_EVENT  equ 27
SYSCALL_YIELD          equ 28
SYSCALL_GUI_RENDER     equ 29
SYSCALL_GUI_DRAW_RECT  equ 30
SYSCALL_GUI_DRAW_TEXT  equ 31
SYSCALL_GUI_GET_MOUSE  equ 33

FONT_WIDTH  equ 8
FONT_HEIGHT equ 16

WIN_W equ 960
WIN_H equ 680

GUI_TITLE_HEIGHT equ 24

TOOLBAR_H     equ 36
NAV_PANE_W    equ 184
COL_HEADER_H  equ 20
ENTRY_START_Y equ 56
ROW_H         equ 16
STATUS_H      equ 24
MAX_VISIBLE   equ 33

SEARCH_W      equ 150
TOOLBAR_BTN_W equ 24
TOOLBAR_BTN_H equ 24
TOOLBAR_BTN_Y equ 6

BACK_X        equ 4
FORWARD_X     equ 30
UP_X          equ 56
REFRESH_X     equ 82
ADDR_X        equ 110
ADDR_H        equ 24
ADDR_Y        equ 6
SEARCH_X      equ WIN_W - 2 - SEARCH_W - 8
SEARCH_Y      equ 6

NAME_X        equ NAV_PANE_W + 8
SIZE_X        equ 460
TYPE_X        equ 620

NAV_ITEM_H    equ 22
NAV_HEADER_H  equ 18
NAV_SEP_H     equ 6

NAV_FAV_HDR   equ TOOLBAR_H
NAV_DESKTOP_Y equ TOOLBAR_H + NAV_HEADER_H
NAV_DOWNLOADS_Y equ NAV_DESKTOP_Y + NAV_ITEM_H
NAV_RECENT_Y  equ NAV_DOWNLOADS_Y + NAV_ITEM_H
NAV_SEP_Y1    equ NAV_RECENT_Y + NAV_ITEM_H
NAV_LIB_HDR   equ NAV_SEP_Y1 + NAV_SEP_H
NAV_DOCS_Y    equ NAV_LIB_HDR + NAV_HEADER_H
NAV_PICS_Y    equ NAV_DOCS_Y + NAV_ITEM_H
NAV_MUSIC_Y   equ NAV_PICS_Y + NAV_ITEM_H
NAV_LAST_END  equ NAV_MUSIC_Y + NAV_ITEM_H

COL_WHITE      equ 0x00FFFFFF
COL_BLACK      equ 0x00000000

COL_TOOLBAR    equ 0x00F0F2F5
COL_TOOLBAR_SEP equ 0x00D0D0D0
COL_ADDR_BG    equ 0x00FFFFFF
COL_ADDR_FG    equ 0x00000000
COL_SEARCH_BG  equ 0x00FFFFFF
COL_SEARCH_FG  equ 0x00808080

COL_BTN_BG     equ 0x00E4E7EC
COL_BTN_BDR    equ 0x00B0B0B0
COL_BTN_FG     equ 0x00333333

COL_NAV_BG     equ 0x00F0F0F0
COL_NAV_SEP    equ 0x00D0D0D0
COL_NAV_HDR    equ 0x00444444
COL_NAV_ITEM   equ 0x00000000

COL_HDR_BG     equ 0x00F5F5F5
COL_HDR_SEP    equ 0x00C8C8C8
COL_HDR_FG     equ 0x00444444

COL_STATUS_BG  equ 0x00E8E8E8
COL_STATUS_FG  equ 0x00333333

COL_FILE_FG    equ 0x00000000
COL_FILE_BG    equ 0x00FFFFFF
COL_DIR_FG     equ 0x001A1A5C
COL_DIR_BG     equ 0x00FFFFFF
COL_ROW_ALT    equ 0x00F0F4F8
COL_HILITE_BG  equ 0x00316AC5
COL_HILITE_FG  equ 0x00FFFFFF

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

    mov rax, SYSCALL_GUI_GET_MOUSE
    lea rdi, [rel mouse_buf]
    syscall

    mov rax, SYSCALL_GUI_GET_WIN_RECT
    mov edi, [rel win_id]
    lea rsi, [rel win_rect]
    syscall

    call compute_hover

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

compute_hover:
    push rax
    push rbx
    push rdx

    mov eax, [rel mouse_buf]
    sub eax, [rel win_rect]
    sub eax, 1
    mov [rel hover_x], eax

    mov eax, [rel mouse_buf + 4]
    sub eax, [rel win_rect + 4]
    sub eax, GUI_TITLE_HEIGHT
    sub eax, 1
    mov [rel hover_y], eax

    mov dword [rel hover_btn], -1
    mov dword [rel hover_nav], -1

    mov eax, [rel hover_y]
    cmp eax, TOOLBAR_H
    jge .not_toolbar
    cmp eax, TOOLBAR_BTN_Y
    jl .not_toolbar
    cmp eax, TOOLBAR_BTN_Y + TOOLBAR_BTN_H
    jge .not_toolbar
    mov eax, [rel hover_x]
    cmp eax, BACK_X
    jl .not_toolbar
    cmp eax, BACK_X + TOOLBAR_BTN_W
    jb .hover_back
    cmp eax, FORWARD_X
    jl .not_toolbar
    cmp eax, FORWARD_X + TOOLBAR_BTN_W
    jb .hover_forward
    cmp eax, UP_X
    jl .not_toolbar
    cmp eax, UP_X + TOOLBAR_BTN_W
    jb .hover_up
    cmp eax, REFRESH_X
    jl .not_toolbar
    cmp eax, REFRESH_X + TOOLBAR_BTN_W
    jb .hover_refresh
    jmp .not_toolbar
.hover_back:
    mov dword [rel hover_btn], 0
    jmp .not_toolbar
.hover_forward:
    mov dword [rel hover_btn], 1
    jmp .not_toolbar
.hover_up:
    mov dword [rel hover_btn], 2
    jmp .not_toolbar
.hover_refresh:
    mov dword [rel hover_btn], 3
.not_toolbar:

    mov eax, [rel hover_x]
    cmp eax, NAV_PANE_W
    jae .not_nav
    mov eax, [rel hover_y]
    cmp eax, NAV_DESKTOP_Y
    jl .not_nav
    cmp eax, NAV_LAST_END
    jge .not_nav
    cmp eax, NAV_DESKTOP_Y + NAV_ITEM_H
    jb .hover_nav0
    cmp eax, NAV_DOWNLOADS_Y
    jl .not_nav
    cmp eax, NAV_DOWNLOADS_Y + NAV_ITEM_H
    jb .hover_nav1
    cmp eax, NAV_RECENT_Y
    jl .not_nav
    cmp eax, NAV_RECENT_Y + NAV_ITEM_H
    jb .hover_nav2
    cmp eax, NAV_DOCS_Y
    jl .not_nav
    cmp eax, NAV_DOCS_Y + NAV_ITEM_H
    jb .hover_nav3
    cmp eax, NAV_PICS_Y
    jl .not_nav
    cmp eax, NAV_PICS_Y + NAV_ITEM_H
    jb .hover_nav4
    cmp eax, NAV_MUSIC_Y
    jl .not_nav
    cmp eax, NAV_MUSIC_Y + NAV_ITEM_H
    jb .hover_nav5
    jmp .not_nav
.hover_nav0:
    mov dword [rel hover_nav], 0
    jmp .not_nav
.hover_nav1:
    mov dword [rel hover_nav], 1
    jmp .not_nav
.hover_nav2:
    mov dword [rel hover_nav], 2
    jmp .not_nav
.hover_nav3:
    mov dword [rel hover_nav], 3
    jmp .not_nav
.hover_nav4:
    mov dword [rel hover_nav], 4
    jmp .not_nav
.hover_nav5:
    mov dword [rel hover_nav], 5
.not_nav:

    pop rdx
    pop rbx
    pop rax
    ret

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
    test eax, eax
    jle .empty
    mov [rel entries_len], eax
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    ret
.empty:
    mov dword [rel entries_len], 0
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
    push r14
    push r15

    call draw_toolbar
    call draw_nav_pane
    call draw_column_headers
    call draw_file_list
    call draw_status_bar

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

draw_toolbar:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10

    mov edi, [rel win_id]
    xor esi, esi
    xor edx, edx
    mov r10d, WIN_W
    mov r8d, TOOLBAR_H
    mov r9d, COL_TOOLBAR
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    ; Back button
    mov edi, [rel win_id]
    mov esi, BACK_X
    mov edx, TOOLBAR_BTN_Y
    mov r10d, TOOLBAR_BTN_W
    mov r8d, TOOLBAR_BTN_H
    mov r9d, COL_BTN_BG
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    mov edi, [rel win_id]
    mov esi, BACK_X
    mov edx, TOOLBAR_BTN_Y
    lea r10, [rel back_icon]
    mov r8d, COL_BTN_FG
    mov r9d, COL_BTN_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Forward button
    mov edi, [rel win_id]
    mov esi, FORWARD_X
    mov edx, TOOLBAR_BTN_Y
    mov r10d, TOOLBAR_BTN_W
    mov r8d, TOOLBAR_BTN_H
    mov r9d, COL_BTN_BG
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    mov edi, [rel win_id]
    mov esi, FORWARD_X
    mov edx, TOOLBAR_BTN_Y
    lea r10, [rel forward_icon]
    mov r8d, COL_BTN_FG
    mov r9d, COL_BTN_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Up button
    mov edi, [rel win_id]
    mov esi, UP_X
    mov edx, TOOLBAR_BTN_Y
    mov r10d, TOOLBAR_BTN_W
    mov r8d, TOOLBAR_BTN_H
    mov r9d, COL_BTN_BG
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    mov edi, [rel win_id]
    mov esi, UP_X
    mov edx, TOOLBAR_BTN_Y
    lea r10, [rel up_icon]
    mov r8d, COL_BTN_FG
    mov r9d, COL_BTN_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Refresh button
    mov edi, [rel win_id]
    mov esi, REFRESH_X
    mov edx, TOOLBAR_BTN_Y
    mov r10d, TOOLBAR_BTN_W
    mov r8d, TOOLBAR_BTN_H
    mov r9d, COL_BTN_BG
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    mov edi, [rel win_id]
    mov esi, REFRESH_X
    mov edx, TOOLBAR_BTN_Y
    lea r10, [rel refresh_icon]
    mov r8d, COL_BTN_FG
    mov r9d, COL_BTN_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Address bar
    lea rdi, [rel addr_buf]
    mov byte [rdi], ' '
    mov byte [rdi + 1], 0
    lea rsi, [rel current_dir]
    lea rdi, [rdi + 1]
    call strcpy

    mov edi, [rel win_id]
    mov esi, ADDR_X
    mov edx, ADDR_Y
    mov r10d, SEARCH_X - ADDR_X - 8
    mov r8d, ADDR_H
    mov r9d, COL_ADDR_BG
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    mov edi, [rel win_id]
    mov esi, ADDR_X
    mov edx, ADDR_Y
    lea r10, [rel addr_buf]
    mov r8d, COL_ADDR_FG
    mov r9d, COL_ADDR_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Search box
    mov edi, [rel win_id]
    mov esi, SEARCH_X
    mov edx, SEARCH_Y
    mov r10d, SEARCH_W
    mov r8d, ADDR_H
    mov r9d, COL_SEARCH_BG
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    mov edi, [rel win_id]
    mov esi, SEARCH_X + 4
    mov edx, SEARCH_Y + 2
    lea r10, [rel search_text]
    mov r8d, COL_SEARCH_FG
    mov r9d, COL_SEARCH_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Bottom separator line
    mov edi, [rel win_id]
    xor esi, esi
    mov edx, TOOLBAR_H - 1
    mov r10d, WIN_W
    mov r8d, 1
    mov r9d, COL_TOOLBAR_SEP
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    ret

draw_nav_pane:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10

    ; Background
    mov edi, [rel win_id]
    xor esi, esi
    mov edx, TOOLBAR_H
    mov r10d, NAV_PANE_W
    mov r8d, WIN_H - TOOLBAR_H - STATUS_H
    mov r9d, COL_NAV_BG
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    ; Right separator line
    mov edi, [rel win_id]
    mov esi, NAV_PANE_W - 1
    mov edx, TOOLBAR_H
    mov r10d, 1
    mov r8d, WIN_H - TOOLBAR_H - STATUS_H
    mov r9d, COL_NAV_SEP
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    ; Favorites header
    mov edi, [rel win_id]
    mov esi, 10
    mov edx, NAV_FAV_HDR
    lea r10, [rel nav_fav_hdr]
    mov r8d, COL_NAV_HDR
    mov r9d, COL_NAV_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Desktop
    mov edi, [rel win_id]
    mov esi, 14
    mov edx, NAV_DESKTOP_Y
    add edx, 2
    lea r10, [rel nav_desk_label]
    mov r8d, COL_NAV_ITEM
    mov r9d, COL_NAV_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Downloads
    mov edi, [rel win_id]
    mov esi, 14
    mov edx, NAV_DOWNLOADS_Y
    add edx, 2
    lea r10, [rel nav_dload_label]
    mov r8d, COL_NAV_ITEM
    mov r9d, COL_NAV_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Recent Places
    mov edi, [rel win_id]
    mov esi, 14
    mov edx, NAV_RECENT_Y
    add edx, 2
    lea r10, [rel nav_recent_label]
    mov r8d, COL_NAV_ITEM
    mov r9d, COL_NAV_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Separator
    mov edi, [rel win_id]
    mov esi, 8
    mov edx, NAV_SEP_Y1
    mov r10d, NAV_PANE_W - 16
    mov r8d, 1
    mov r9d, COL_NAV_SEP
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    ; Libraries header
    mov edi, [rel win_id]
    mov esi, 10
    mov edx, NAV_LIB_HDR
    lea r10, [rel nav_lib_hdr]
    mov r8d, COL_NAV_HDR
    mov r9d, COL_NAV_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Documents
    mov edi, [rel win_id]
    mov esi, 14
    mov edx, NAV_DOCS_Y
    add edx, 2
    lea r10, [rel nav_docs_label]
    mov r8d, COL_NAV_ITEM
    mov r9d, COL_NAV_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Pictures
    mov edi, [rel win_id]
    mov esi, 14
    mov edx, NAV_PICS_Y
    add edx, 2
    lea r10, [rel nav_pics_label]
    mov r8d, COL_NAV_ITEM
    mov r9d, COL_NAV_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Music
    mov edi, [rel win_id]
    mov esi, 14
    mov edx, NAV_MUSIC_Y
    add edx, 2
    lea r10, [rel nav_music_label]
    mov r8d, COL_NAV_ITEM
    mov r9d, COL_NAV_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    ret

draw_column_headers:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10

    ; Background
    mov edi, [rel win_id]
    mov esi, NAV_PANE_W
    mov edx, TOOLBAR_H
    mov r10d, WIN_W - NAV_PANE_W
    mov r8d, COL_HEADER_H
    mov r9d, COL_HDR_BG
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    ; Name header
    mov edi, [rel win_id]
    mov esi, NAME_X
    mov edx, TOOLBAR_H + 2
    lea r10, [rel hdr_name]
    mov r8d, COL_HDR_FG
    mov r9d, COL_HDR_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Size header
    mov edi, [rel win_id]
    mov esi, SIZE_X
    mov edx, TOOLBAR_H + 2
    lea r10, [rel hdr_size]
    mov r8d, COL_HDR_FG
    mov r9d, COL_HDR_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Type header
    mov edi, [rel win_id]
    mov esi, TYPE_X
    mov edx, TOOLBAR_H + 2
    lea r10, [rel hdr_type]
    mov r8d, COL_HDR_FG
    mov r9d, COL_HDR_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    ; Bottom separator
    mov edi, [rel win_id]
    mov esi, NAV_PANE_W
    mov edx, TOOLBAR_H + COL_HEADER_H - 1
    mov r10d, WIN_W - NAV_PANE_W
    mov r8d, 1
    mov r9d, COL_HDR_SEP
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    ; Top separator
    mov edi, [rel win_id]
    mov esi, NAV_PANE_W
    mov edx, TOOLBAR_H
    mov r10d, WIN_W - NAV_PANE_W
    mov r8d, 1
    mov r9d, COL_HDR_SEP
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    ; Column separators
    mov edi, [rel win_id]
    mov esi, SIZE_X - 8
    mov edx, TOOLBAR_H + 1
    mov r10d, 1
    mov r8d, COL_HEADER_H - 2
    mov r9d, COL_HDR_SEP
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    mov edi, [rel win_id]
    mov esi, TYPE_X - 8
    mov edx, TOOLBAR_H + 1
    mov r10d, 1
    mov r8d, COL_HEADER_H - 2
    mov r9d, COL_HDR_SEP
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    ret

draw_file_list:
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

    mov eax, [rel entries_len]
    test eax, eax
    jle .list_done

    ; Fill content background
    mov edi, [rel win_id]
    mov esi, NAV_PANE_W
    mov edx, ENTRY_START_Y
    mov r10d, WIN_W - NAV_PANE_W
    mov r8d, MAX_VISIBLE * ROW_H + ROW_H
    mov r9d, COL_FILE_BG
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    xor r14d, r14d
    lea r14, [rel entries_buf]
    xor r15d, r15d

.list_loop:
    mov al, [r14]
    test al, al
    jz .list_done
    cmp al, 'D'
    je .list_dir

    cmp r15d, MAX_VISIBLE
    jae .skip_entry
    call draw_file_row
    inc r15d
    jmp .skip_entry

.list_dir:
    lea rdi, [r14 + 2]
    call strlen
    cmp rax, 1
    jne .draw_dir
    lea rdi, [r14 + 2]
    cmp byte [rdi], '.'
    je .skip_entry

.draw_dir:
    cmp r15d, MAX_VISIBLE
    jae .skip_entry
    call draw_dir_row
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
    jmp .list_loop

.list_done:
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

draw_dir_row:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10

    mov eax, r15d
    and eax, 1
    jz .dir_row_no_alt
    mov edi, [rel win_id]
    mov esi, NAV_PANE_W
    mov edx, ENTRY_START_Y
    imul edx, r15d, ROW_H
    add edx, ENTRY_START_Y
    mov r10d, WIN_W - NAV_PANE_W
    mov r8d, ROW_H
    mov r9d, COL_ROW_ALT
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall
.dir_row_no_alt:

    lea rdi, [rel row_buf]
    mov byte [rdi], ' '
    mov byte [rdi + 1], 0
    lea rsi, [r14 + 2]
    lea rdi, [rdi + 1]
    call strcpy

    mov edi, [rel win_id]
    mov esi, NAME_X
    mov edx, ENTRY_START_Y
    imul edx, r15d, ROW_H
    add edx, ENTRY_START_Y
    lea r10, [rel row_buf]
    mov r8d, COL_DIR_FG
    mov r9d, COL_FILE_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    mov edi, [rel win_id]
    mov esi, SIZE_X
    mov edx, ENTRY_START_Y
    imul edx, r15d, ROW_H
    add edx, ENTRY_START_Y
    lea r10, [rel folder_label]
    mov r8d, COL_HDR_FG
    mov r9d, COL_FILE_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    ret

draw_file_row:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10

    mov eax, r15d
    and eax, 1
    jz .file_row_no_alt
    mov edi, [rel win_id]
    mov esi, NAV_PANE_W
    mov edx, ENTRY_START_Y
    imul edx, r15d, ROW_H
    add edx, ENTRY_START_Y
    mov r10d, WIN_W - NAV_PANE_W
    mov r8d, ROW_H
    mov r9d, COL_ROW_ALT
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall
.file_row_no_alt:

    lea rdi, [rel row_buf]
    mov byte [rdi], ' '
    mov byte [rdi + 1], 0
    lea rsi, [r14 + 2]
    lea rdi, [rdi + 1]
    call strcpy

    mov edi, [rel win_id]
    mov esi, NAME_X
    mov edx, ENTRY_START_Y
    imul edx, r15d, ROW_H
    add edx, ENTRY_START_Y
    lea r10, [rel row_buf]
    mov r8d, COL_FILE_FG
    mov r9d, COL_FILE_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    lea rdi, [r14 + 2]
    call strlen
    lea rdi, [r14 + 2 + rax + 1]
    cmp byte [rdi], 0
    je .file_row_no_size

    mov edi, [rel win_id]
    mov esi, SIZE_X
    mov edx, ENTRY_START_Y
    imul edx, r15d, ROW_H
    add edx, ENTRY_START_Y
    mov r10, rdi
    mov r8d, COL_FILE_FG
    mov r9d, COL_FILE_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall
.file_row_no_size:

    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    ret

draw_status_bar:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10

    mov eax, WIN_H
    sub eax, GUI_TITLE_HEIGHT
    sub eax, 3
    sub eax, STATUS_H

    mov edi, [rel win_id]
    xor esi, esi
    mov edx, eax
    mov r10d, WIN_W
    mov r8d, STATUS_H
    mov r9d, COL_STATUS_BG
    mov rax, SYSCALL_GUI_DRAW_RECT
    syscall

    mov edi, [rel win_id]
    xor esi, esi
    mov edx, eax
    add edx, 2
    lea r10, [rel items_label]
    mov r8d, COL_STATUS_FG
    mov r9d, COL_STATUS_BG
    mov rax, SYSCALL_GUI_DRAW_TEXT
    syscall

    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
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
    push r14
    push r15

    mov eax, [rel event_buf + 4]
    sub eax, [rel win_rect]
    sub eax, 1
    mov [rel rel_x], eax

    mov eax, [rel event_buf + 8]
    sub eax, [rel win_rect + 4]
    sub eax, GUI_TITLE_HEIGHT
    sub eax, 1
    mov [rel rel_y], eax

    ; Toolbar click
    mov eax, [rel rel_y]
    test eax, eax
    js .event_done
    cmp eax, TOOLBAR_H
    jge .check_nav_pane
    cmp eax, TOOLBAR_BTN_Y
    jl .check_nav_pane
    cmp eax, TOOLBAR_BTN_Y + TOOLBAR_BTN_H
    jge .check_nav_pane
    mov eax, [rel rel_x]
    cmp eax, UP_X
    jl .check_bf
    cmp eax, UP_X + TOOLBAR_BTN_W
    jb .do_up
    cmp eax, REFRESH_X
    jl .check_bf
    cmp eax, REFRESH_X + TOOLBAR_BTN_W
    jb .do_refresh
    jmp .check_bf
.do_up:
    call go_up_dir
    jmp .event_done
.do_refresh:
    jmp .event_done
.check_bf:

.check_nav_pane:
    mov eax, [rel rel_x]
    cmp eax, NAV_PANE_W
    jae .check_file_list
    mov eax, [rel rel_y]
    cmp eax, NAV_DESKTOP_Y
    jl .check_file_list
    cmp eax, NAV_DESKTOP_Y + NAV_ITEM_H
    jb .nav_to_root
    cmp eax, NAV_DOWNLOADS_Y
    jl .check_file_list
    cmp eax, NAV_DOWNLOADS_Y + NAV_ITEM_H
    jb .nav_to_downloads
    cmp eax, NAV_RECENT_Y
    jl .check_file_list
    cmp eax, NAV_RECENT_Y + NAV_ITEM_H
    jb .nav_to_root
    cmp eax, NAV_DOCS_Y
    jl .check_file_list
    cmp eax, NAV_DOCS_Y + NAV_ITEM_H
    jb .nav_to_system
    cmp eax, NAV_PICS_Y
    jl .check_file_list
    cmp eax, NAV_PICS_Y + NAV_ITEM_H
    jb .nav_to_system
    cmp eax, NAV_MUSIC_Y
    jl .check_file_list
    cmp eax, NAV_MUSIC_Y + NAV_ITEM_H
    jb .nav_to_system
    jmp .check_file_list

.nav_to_root:
    lea rdi, [rel root_path]
    call set_dir
    jmp .event_done
.nav_to_downloads:
    lea rdi, [rel downloads_path]
    call set_dir
    jmp .event_done
.nav_to_system:
    lea rdi, [rel system_path]
    call set_dir
    jmp .event_done

.check_file_list:
    mov eax, [rel rel_x]
    cmp eax, NAV_PANE_W
    jl .event_done
    mov eax, [rel rel_y]
    cmp eax, ENTRY_START_Y
    jl .event_done

    sub eax, ENTRY_START_Y
    xor edx, edx
    mov ecx, ROW_H
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

    cmp r15d, [rel click_entry]
    jne .not_match_file
    call exec_entry
    jmp .event_done
.not_match_file:
    inc r15d
    jmp .advance

.found_dir:
    lea rdi, [r14 + 2]
    call strlen
    cmp rax, 1
    jne .check_dir_click
    lea rdi, [r14 + 2]
    cmp byte [rdi], '.'
    je .advance

.check_dir_click:
    cmp r15d, [rel click_entry]
    jne .not_match_dir
    call navigate_dir
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

set_dir:
    push rax
    push rsi
    push rdi

    pop rsi
    lea rdi, [rel current_dir]
    call strcpy

    pop rsi
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

title_str:      db "Scout - File Browser", 0
back_icon:      db "<", 0
forward_icon:   db ">", 0
up_icon:        db "^", 0
refresh_icon:   db "*", 0
search_text:    db "Search", 0
folder_label:   db "File folder", 0

hdr_name:       db "Name", 0
hdr_size:       db "Size", 0
hdr_type:       db "Type", 0

items_label:    db "Items", 0

nav_fav_hdr:    db "  Favorites", 0
nav_lib_hdr:    db "  Libraries", 0
nav_desk_label: db "Desktop", 0
nav_dload_label: db "Downloads", 0
nav_recent_label: db "Recent Places", 0
nav_docs_label: db "Documents", 0
nav_pics_label: db "Pictures", 0
nav_music_label: db "Music", 0

root_path:      db "\", 0
downloads_path: db "\SYSTEM\DRIVERS\", 0
system_path:    db "\SYSTEM\", 0

align 8
win_id:             dd 0
current_dir:        times 256 db 0
path_buf:           times 512 db 0
entries_buf:        times 4096 db 0
entries_len:        dd 0
win_rect:           times 16 db 0
event_buf:          times 16 db 0
mouse_buf:          times 12 db 0
rel_x:              dd 0
rel_y:              dd 0
hover_x:            dd 0
hover_y:            dd 0
hover_btn:          dd -1
hover_nav:          dd -1
click_entry:        dd 0
row_buf:            times 128 db 0
addr_buf:           times 260 db 0
