BITS 64

SYSCALL_PRINT   equ 3
SYSCALL_OPEN    equ 6
SYSCALL_READ    equ 7
SYSCALL_WRITE   equ 8
SYSCALL_CLOSE   equ 9
SYSCALL_GETCHAR equ 10
SYSCALL_CLS     equ 11
SYSCALL_EXIT    equ 1

O_READ  equ 1
O_WRITE equ 2
O_CREAT equ 4

MAX_LINES      equ 256
LINE_BUF_SIZE  equ 65536
FILENAME_MAX   equ 256
CMD_BUF_SIZE   equ 256

; ============================================================
; Entry point
; ============================================================
start:
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8

    mov rax, SYSCALL_CLS
    syscall

    lea rdi, [rel banner]
    call print_str

    ; clear line_offsets to -1
    lea rdi, [rel line_offsets]
    mov ecx, MAX_LINES
    mov eax, -1
.clear_loop:
    mov [rdi], eax
    add rdi, 4
    dec ecx
    jnz .clear_loop

    ; init state
    mov qword [rel line_count], 0
    mov qword [rel line_data_end], 0
    mov byte [rel modified], 0
    mov byte [rel force_flag], 0
    mov byte [rel filename], 0

    ; prompt for filename
    lea rdi, [rel msg_filename]
    call print_str
    lea rdi, [rel filename]
    mov byte [rdi], 0
    call readline
    lea rdi, [rel filename]
    cmp byte [rdi], 0
    je .main_loop
    call cmd_edit_file

.main_loop:
    lea rdi, [rel prompt]
    call print_str
    lea rdi, [rel cmd_buf]
    call readline

    lea rsi, [rel cmd_buf]
    call skip_spaces
    mov rsi, rax
    cmp byte [rsi], 0
    je .main_loop

    ; parse range
    push rsi
    call parse_range
    mov r13, rax        ; r13 = from (-1 if none)
    mov r14, rdx        ; r14 = to (-1 if none)
    pop rsi

    call skip_spaces
    mov rsi, rax
    movzx r8, byte [rsi]

    cmp r8b, 'q'
    je .do_quit
    cmp r8b, 'Q'
    je .do_quit
    cmp r8b, 'h'
    je .do_help
    cmp r8b, 'H'
    je .do_help
    cmp r8b, '?'
    je .do_help

    cmp r8b, 'p'
    je .do_print
    cmp r8b, 'P'
    je .do_print

    cmp r8b, 'a'
    je .do_append
    cmp r8b, 'A'
    je .do_append

    cmp r8b, 'i'
    je .do_insert
    cmp r8b, 'I'
    je .do_insert

    cmp r8b, 'd'
    je .do_delete
    cmp r8b, 'D'
    je .do_delete

    cmp r8b, 'w'
    je .do_write
    cmp r8b, 'W'
    je .do_write

    cmp r8b, 'e'
    je .do_edit
    cmp r8b, 'E'
    je .do_edit

    ; if there's a number but no command, default to print
    cmp r13, -1
    je .unknown_cmd
    cmp r8b, 0
    jne .unknown_cmd
    jmp .do_print

.unknown_cmd:
    lea rdi, [rel msg_unknown]
    call print_str
    jmp .main_loop

; ============================================================
; Command handlers
; ============================================================

.do_help:
    lea rdi, [rel help_text]
    call print_str
    jmp .main_loop

.do_quit:
    inc rsi
    call skip_spaces
    mov rsi, rax
    mov byte [rel force_flag], 0
    cmp byte [rsi], '!'
    jne .quit_check
    mov byte [rel force_flag], 1

.quit_check:
    cmp byte [rel modified], 0
    je .quit_ok
    cmp byte [rel force_flag], 0
    jne .quit_ok
    lea rdi, [rel msg_modified]
    call print_str
    jmp .main_loop

.quit_ok:
    xor edi, edi
    mov rax, SYSCALL_EXIT
    syscall

.do_print:
    ; if r13/r14 are -1, set defaults
    cmp r13, -1
    jne .pr1
    mov r13, 1
.pr1:
    cmp r14, -1
    jne .pr2
    mov r14, r13
    cmp r13, 1
    jne .pr2
    mov r14, qword [rel line_count]
.pr2:
    mov rdi, r13
    mov rsi, r14
    call print_lines
    jmp .main_loop

.do_append:
    ; default: append after last line
    cmp r13, -1
    jne .ap1
    mov r13, qword [rel line_count]
    inc r13
.ap1:
    mov rdi, r13
    call cmd_append
    jmp .main_loop

.do_insert:
    ; default: insert before line 1
    cmp r13, -1
    jne .in1
    mov r13, 1
.in1:
    mov rdi, r13
    call cmd_insert
    jmp .main_loop

.do_delete:
    cmp r13, -1
    jne .de1
    mov r13, qword [rel line_count]
.de1:
    cmp r14, -1
    jne .de2
    mov r14, r13
.de2:
    mov rdi, r13
    mov rsi, r14
    call cmd_delete
    jmp .main_loop

.do_write:
    inc rsi
    call skip_spaces
    mov rsi, rax
    cmp byte [rsi], 0
    je .wr_current
    lea rdi, [rel filename]
    call strcpy
.wr_current:
    lea rdi, [rel filename]
    cmp byte [rdi], 0
    je .wr_err
    call cmd_write_file
    jmp .main_loop
.wr_err:
    lea rdi, [rel msg_no_filename]
    call print_str
    jmp .main_loop

.do_edit:
    inc rsi
    call skip_spaces
    mov rsi, rax
    cmp byte [rsi], 0
    je .ed_err
    lea rdi, [rel filename]
    call strcpy
    lea rdi, [rel filename]
    call cmd_edit_file
    jmp .main_loop
.ed_err:
    lea rdi, [rel msg_no_filename]
    call print_str
    jmp .main_loop

; ============================================================
; parse_range: parse [from][,[to]] from string at rsi
; output: rax = from (-1 if none), rdx = to (-1 if none)
; ============================================================
parse_range:
    push rbx
    push rsi
    call skip_spaces
    mov rsi, rax
    xor ecx, ecx
    xor r8d, r8d

    ; try first number
    mov rdi, rsi
    call str_to_num
    cmp rdx, 0
    je .pr_none
    mov rcx, rax
    mov rsi, rdx

    ; check for comma
    call skip_spaces
    mov rsi, rax
    cmp byte [rsi], ','
    jne .pr_single

    inc rsi
    call skip_spaces
    mov rsi, rax
    mov rdi, rsi
    call str_to_num
    cmp rdx, 0
    je .pr_single
    mov r8, rax
    mov rsi, rdx
    jmp .pr_done

.pr_none:
    xor ecx, ecx
    dec ecx          ; rcx = -1
.pr_single:
    mov r8, rcx

.pr_done:
    mov rax, rcx
    mov rdx, r8
    pop rsi
    pop rbx
    ret

; ============================================================
; print_str
; ============================================================
print_str:
    push rdi
    mov rax, SYSCALL_PRINT
    syscall
    pop rdi
    ret

; ============================================================
; readline: read keyboard line into buffer at rdi
; ============================================================
readline:
    push rbx
    push r12
    mov r12, rdi
    xor ebx, ebx

.rl_loop:
    mov rax, SYSCALL_GETCHAR
    syscall
    cmp al, 13
    je .rl_done
    cmp al, 10
    je .rl_done
    cmp al, 8
    je .rl_bs
    cmp al, 127
    je .rl_bs
    cmp bl, CMD_BUF_SIZE - 2
    jae .rl_loop

    mov [r12 + rbx], al
    inc bl

    mov byte [rel echobuf], al
    mov byte [rel echobuf + 1], 0
    push rdi
    lea rdi, [rel echobuf]
    call print_str
    pop rdi
    jmp .rl_loop

.rl_bs:
    cmp bl, 0
    je .rl_loop
    dec bl
    push rdi
    lea rdi, [rel bs_echo]
    call print_str
    pop rdi
    jmp .rl_loop

.rl_done:
    mov byte [r12 + rbx], 0
    push rdi
    lea rdi, [rel newline_str]
    call print_str
    pop rdi
    mov rax, r12
    pop r12
    pop rbx
    ret

; ============================================================
; str_to_num: parse decimal number at rdi
; output: rax = number, rdx = ptr past number (0 if no digits)
; ============================================================
str_to_num:
    push rdi
    call skip_spaces
    mov rdi, rax
    xor eax, eax
    xor ecx, ecx
.loop:
    movzx r8, byte [rdi]
    cmp r8b, '0'
    jb .done
    cmp r8b, '9'
    ja .done
    imul rax, 10
    sub r8b, '0'
    add rax, r8
    inc rdi
    inc ecx
    jmp .loop
.done:
    test ecx, ecx
    jnz .have
    xor edx, edx
    pop rdi
    ret
.have:
    mov rdx, rdi
    pop rdi
    ret

; ============================================================
; skip_spaces: skip leading spaces in string at rsi
; output: rax = pointer past spaces
; ============================================================
skip_spaces:
.loop:
    cmp byte [rsi], ' '
    je .next
    cmp byte [rsi], 9
    jne .done
.next:
    inc rsi
    jmp .loop
.done:
    mov rax, rsi
    ret

; ============================================================
; strcpy
; ============================================================
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

; ============================================================
; strlen
; ============================================================
strlen:
    push rdi
    xor eax, eax
.loop:
    cmp byte [rdi], 0
    je .done
    inc rdi
    inc eax
    jmp .loop
.done:
    pop rdi
    ret

; ============================================================
; print_lines: display lines from (1-based) to (1-based, -1=all)
; ============================================================
print_lines:
    push rbx
    push r12
    push r13
    push r14
    push r15

    cmp rsi, -1
    jne .pl1
    mov rsi, qword [rel line_count]
.pl1:
    cmp rdi, 1
    jae .pl2
    mov rdi, 1
.pl2:
    cmp rsi, qword [rel line_count]
    jbe .pl3
    mov rsi, qword [rel line_count]
.pl3:
    cmp rdi, rsi
    ja .pl_empty

    mov r12, rdi
    mov r13, rsi

.pl_loop:
    cmp r12, r13
    ja .pl_done

    lea rbx, [rel line_offsets]
    mov eax, r12d
    dec eax
    mov eax, [rbx + rax * 4]
    cmp eax, -1
    je .pl_next

    ; print line number
    sub rsp, 16
    mov rdi, r12
    lea rsi, [rsp]
    call num_to_str
    lea rdi, [rsp]
    call print_str
    lea rdi, [rel colon_space]
    call print_str
    add rsp, 16

    ; print line content: line_buf + offset
    lea rdi, [rel line_buf]
    mov eax, r12d
    dec eax
    mov eax, [rbx + rax * 4]
    add rdi, rax
    call print_str
    lea rdi, [rel newline_str]
    call print_str

.pl_next:
    inc r12
    jmp .pl_loop

.pl_done:
    cmp qword [rel line_count], 0
    jne .pl_exit
.pl_empty:
    lea rdi, [rel msg_empty]
    call print_str

.pl_exit:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; ============================================================
; num_to_str: convert unsigned int to string
; ============================================================
num_to_str:
    push rbx
    push r12
    push r13
    mov r12, rsi
    mov rax, rdi
    xor ecx, ecx
    mov ebx, 10

.reverse:
    xor edx, edx
    div rbx
    add dl, '0'
    push rdx
    inc ecx
    test rax, rax
    jnz .reverse

    xor r13d, r13d
.write:
    pop rax
    mov [r12 + r13], al
    inc r13d
    dec ecx
    jnz .write
    mov byte [r12 + r13], 0

    pop r13
    pop r12
    pop rbx
    ret

; ============================================================
; cmd_append: read lines and append after line N (rdi)
; ============================================================
cmd_append:
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, rdi       ; insert after this line

    lea rdi, [rel msg_append_mode]
    call print_str

    mov r13, r12       ; current insert position

.ap_loop:
    lea rdi, [rel cmd_buf]
    call readline
    lea rbx, [rel cmd_buf]

    cmp byte [rbx], '.'
    jne .ap_not_dot
    cmp byte [rbx + 1], 0
    je .ap_done
.ap_not_dot:

    mov rdi, rbx
    mov r12, r13
    call insert_line_at
    inc r13

    jmp .ap_loop

.ap_done:
    mov byte [rel modified], 1
    lea rdi, [rel msg_ok]
    call print_str

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; ============================================================
; cmd_insert: read lines and insert before line N (rdi)
; ============================================================
cmd_insert:
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, rdi
    cmp r12, 1
    jae .in_ok
    mov r12, 1
.in_ok:
    mov r13, r12

    lea rdi, [rel msg_append_mode]
    call print_str

.in_loop:
    lea rdi, [rel cmd_buf]
    call readline
    lea rbx, [rel cmd_buf]

    cmp byte [rbx], '.'
    jne .in_not_dot
    cmp byte [rbx + 1], 0
    je .in_done
.in_not_dot:

    mov rdi, rbx
    mov r12, r13
    call insert_line_at
    inc r13
    inc r12
    jmp .in_loop

.in_done:
    mov byte [rel modified], 1
    lea rdi, [rel msg_ok]
    call print_str

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; ============================================================
; insert_line_at: insert string at given line number
; rdi = string to insert, r12 = target line number (1-based)
; ============================================================
insert_line_at:
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r13, rdi
    mov r14, r12
    dec r14                 ; 0-based
    js .ins_done

    ; get string length
    mov rdi, r13
    call strlen
    mov r15, rax
    inc r15                 ; +1 for null

    ; check max lines
    cmp qword [rel line_count], MAX_LINES - 1
    jb .ins_ok
    lea rdi, [rel msg_max_lines]
    call print_str
    jmp .ins_done

.ins_ok:
    ; shift offsets from last down to r14
    lea rbx, [rel line_offsets]
    mov eax, [rel line_count]
    dec eax
.ins_shift:
    cmp eax, r14d
    jb .ins_shift_done
    mov ecx, [rbx + rax * 4]
    mov [rbx + rax * 4 + 4], ecx
    dec eax
    jmp .ins_shift
.ins_shift_done:

    ; store new offset
    mov eax, [rel line_data_end]
    mov [rbx + r14 * 4], eax

    ; copy string to line_buf
    lea rdi, [rel line_buf]
    mov eax, [rel line_data_end]
    add rdi, rax
    mov rsi, r13
    call strcpy

    ; update line_data_end
    mov eax, [rel line_data_end]
    add eax, r15d
    mov [rel line_data_end], eax

    inc qword [rel line_count]

.ins_done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; ============================================================
; cmd_delete: delete lines from rdi to rsi (1-based)
; ============================================================
cmd_delete:
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, rdi
    mov r13, rsi

    ; validate
    cmp r12, 1
    jb .del_done
    cmp r13, r12
    jb .del_done
    mov rax, qword [rel line_count]
    cmp r12, rax
    ja .del_done
    cmp r13, rax
    ja .del_done

    ; convert to 0-based
    dec r12
    dec r13

    ; number of lines to delete
    mov r14, r13
    sub r14, r12
    inc r14

    ; get offsets
    lea rbx, [rel line_offsets]
    mov eax, [rbx + r12 * 4]        ; delete_start offset
    mov r15, rax

    ; delete_end = line_data_end if deleting to end
    mov eax, r13d
    inc eax
    cmp eax, [rel line_count]
    jne .del_mid
    mov rdi, qword [rel line_data_end]
    jmp .del_have_end
.del_mid:
    mov eax, [rbx + rax * 4]        ; offset after deletion
    mov rdi, rax
.del_have_end:
    ; rdi = data after deleted section

    ; bytes to remove = delete_end - delete_start
    mov eax, edi
    sub eax, r15d
    mov r8, rax                     ; r8 = bytes_to_remove

    ; bytes to shift = line_data_end - delete_end
    mov rax, qword [rel line_data_end]
    sub rax, rdi
    mov r9, rax                     ; r9 = bytes_to_shift

    ; shift line_buf: memmove(delete_start, delete_end, bytes_to_shift)
    lea rsi, [rel line_buf]
    add rsi, rdi                    ; source = line_buf + delete_end
    lea rdi, [rel line_buf]
    add rdi, r15                    ; dest = line_buf + delete_start
    mov rcx, r9
    rep movsb

    ; update line_data_end
    mov rax, qword [rel line_data_end]
    sub rax, r8
    mov [rel line_data_end], rax

    ; shift offsets: move entries at r13+1 down by r14 entries
    ; Also subtract bytes_to_remove from all offsets after deletion
    mov eax, r13d
    inc eax
    mov ecx, eax                    ; ecx = index after deletion

.del_shift_loop:
    cmp ecx, [rel line_count]
    jae .del_shift_done

    mov eax, [rbx + rcx * 4]       ; read offset at ecx
    sub eax, r8d                   ; adjust for removal
    mov [rbx + r12 * 4], eax       ; write at r12 position
    inc r12
    inc ecx
    jmp .del_shift_loop
.del_shift_done:

    ; clear remaining offsets
    mov eax, [rel line_count]
    sub eax, r14d
.del_clear:
    cmp eax, MAX_LINES
    jae .del_clear_done
    mov dword [rbx + rax * 4], -1
    inc eax
    jmp .del_clear
.del_clear_done:

    ; update line_count
    mov rax, qword [rel line_count]
    sub rax, r14
    mov [rel line_count], rax

    mov byte [rel modified], 1
    lea rdi, [rel msg_ok]
    call print_str

.del_done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; ============================================================
; cmd_write_file: write lines to file (rdi = filename)
; ============================================================
cmd_write_file:
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, rdi

    mov rax, SYSCALL_OPEN
    mov rdi, r12
    mov rsi, O_WRITE | O_CREAT
    syscall

    cmp rax, 0
    jl .wr_fail
    mov r13, rax            ; fd

    xor r14d, r14d          ; line index

.wr_loop:
    cmp r14, qword [rel line_count]
    jae .wr_done

    lea rbx, [rel line_offsets]
    mov eax, [rbx + r14 * 4]
    cmp eax, -1
    je .wr_next

    ; get line address: line_buf + offset
    push r14
    lea r14, [rel line_buf]
    add r14, rax
    mov rdi, r14
    call strlen
    mov r15, rax
    pop r14

    ; write line content
    mov rax, SYSCALL_WRITE
    mov rdi, r13
    lea rsi, [rel line_buf]
    mov eax, [rbx + r14 * 4]
    add rsi, rax
    mov rdx, r15
    syscall

    ; write crlf
    mov rax, SYSCALL_WRITE
    mov rdi, r13
    lea rsi, [rel crlf]
    mov rdx, 2
    syscall

.wr_next:
    inc r14
    jmp .wr_loop

.wr_done:
    mov rax, SYSCALL_CLOSE
    mov rdi, r13
    syscall

    mov byte [rel modified], 0
    lea rdi, [rel msg_written]
    call print_str
    mov rax, qword [rel line_count]
    sub rsp, 16
    mov rdi, rax
    lea rsi, [rsp]
    call num_to_str
    lea rdi, [rsp]
    call print_str
    lea rdi, [rel msg_lines]
    call print_str
    add rsp, 16

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

.wr_fail:
    lea rdi, [rel msg_write_fail]
    call print_str
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; ============================================================
; cmd_edit_file: load file into line buffer (rdi = filename)
; ============================================================
cmd_edit_file:
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, rdi

    ; open file
    mov rax, SYSCALL_OPEN
    mov rdi, r12
    xor esi, esi
    syscall

    cmp rax, 0
    jl .ed_fail
    mov r13, rax            ; fd

    ; clear line_buf
    lea rdi, [rel line_buf]
    mov ecx, LINE_BUF_SIZE
    xor al, al
    rep stosb

    ; read file
    mov rax, SYSCALL_READ
    mov rdi, r13
    lea rsi, [rel line_buf]
    mov rdx, LINE_BUF_SIZE - 1
    syscall

    mov rbx, rax            ; bytes read

    mov rax, SYSCALL_CLOSE
    mov rdi, r13
    syscall

    cmp rbx, 0
    jle .ed_empty

    ; parse lines: replace \r\n, \n, \r with null
    lea rdi, [rel line_offsets]
    mov dword [rdi], 0       ; first line at offset 0
    mov ecx, 1               ; line count

    xor esi, esi             ; byte index in line_buf
    lea rdi, [rel line_buf]

.ed_parse:
    cmp esi, ebx
    jae .ed_parse_done

    mov al, [rdi + rsi]
    cmp al, 13
    je .ed_cr
    cmp al, 10
    je .ed_lf
    inc esi
    jmp .ed_parse

.ed_cr:
    mov byte [rdi + rsi], 0
    inc esi
    cmp esi, ebx
    jae .ed_newline
    cmp byte [rdi + rsi], 10
    jne .ed_newline
    inc esi
    jmp .ed_newline

.ed_lf:
    mov byte [rdi + rsi], 0
    inc esi
    jmp .ed_newline

.ed_newline:
    cmp ecx, MAX_LINES
    jae .ed_parse
    lea rdi, [rel line_offsets]
    mov [rdi + rcx * 4], esi
    inc ecx
    lea rdi, [rel line_buf]
    jmp .ed_parse

.ed_parse_done:
    mov [rel line_count], ecx
    mov qword [rel line_data_end], rbx
    mov byte [rel modified], 0

    lea rdi, [rel msg_loaded]
    call print_str
    mov rdi, rcx
    sub rsp, 16
    lea rsi, [rsp]
    call num_to_str
    lea rdi, [rsp]
    call print_str
    lea rdi, [rel msg_lines]
    call print_str
    add rsp, 16

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

.ed_empty:
    xor ecx, ecx
    jmp .ed_parse_done

.ed_fail:
    lea rdi, [rel msg_load_fail]
    call print_str
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

; ============================================================
; Data
; ============================================================
align 8
banner:
db "BlueOS Line Editor v1.0", 13, 10
db "Type 'h' for help, 'q' to quit.", 13, 10, 13, 10, 0

msg_filename:
db "Filename: ", 0

prompt:
db "> ", 0

colon_space:
db ": ", 0

newline_str:
db 13, 10, 0

crlf:
db 13, 10

msg_unknown:
db "?", 13, 10, 0

msg_modified:
db "Buffer modified! Use 'q!' to quit without saving.", 13, 10, 0

msg_empty:
db "[empty buffer]", 13, 10, 0

msg_no_filename:
db "No filename specified.", 13, 10, 0

msg_append_mode:
db "(Enter lines, '.' alone to end)", 13, 10, 0

msg_ok:
db "OK", 13, 10, 0

msg_max_lines:
db "Maximum lines reached.", 13, 10, 0

msg_written:
db "Wrote ", 0

msg_lines:
db " lines.", 13, 10, 0

msg_write_fail:
db "Write failed!", 13, 10, 0

msg_loaded:
db "Loaded ", 0

msg_load_fail:
db "File not found or load failed.", 13, 10, 0

bs_echo:
db 8, ' ', 8, 0

echobuf:
db 0, 0

help_text:
db 13, 10
db "  p        Print all lines with numbers", 13, 10
db "  N p      Print line N", 13, 10
db "  N,M p    Print lines N through M", 13, 10
db "  a        Append lines after last line", 13, 10
db "  N a      Append lines after line N", 13, 10
db "  N i      Insert lines before line N", 13, 10
db "  N d      Delete line N", 13, 10
db "  N,M d    Delete lines N through M", 13, 10
db "  w        Write to current file", 13, 10
db "  w FILE   Write to FILE", 13, 10
db "  e FILE   Edit (load) FILE", 13, 10
db "  q        Quit (if not modified)", 13, 10
db "  q!       Force quit without saving", 13, 10
db "  h        This help", 13, 10
db "  ?        This help", 13, 10
db 13, 10
db "If no command letter is given, 'p' is assumed.", 13, 10
db "Examples: 3  -> print line 3", 13, 10
db "          1,5 -> print lines 1-5", 13, 10
db 13, 10, 0

force_flag:
db 0

align 4
line_offsets:
times MAX_LINES dd -1

line_count:
dq 0

line_data_end:
dq 0

modified:
db 0

align 4
filename:
times FILENAME_MAX db 0

cmd_buf:
times CMD_BUF_SIZE db 0

line_buf:
times LINE_BUF_SIZE db 0
