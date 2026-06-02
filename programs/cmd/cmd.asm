BITS 64

SYSCALL_EXIT        equ 1
SYSCALL_PRINT       equ 3
SYSCALL_OPEN        equ 6
SYSCALL_READ        equ 7
SYSCALL_WRITE       equ 8
SYSCALL_CLOSE       equ 9
SYSCALL_GETCHAR     equ 10
SYSCALL_CREATE_TERM equ 14
SYSCALL_CLR_TERM    equ 15
SYSCALL_READDIR     equ 16
SYSCALL_PE_CHECK    equ 17
SYSCALL_EXEC_WAIT   equ 18
SYSCALL_EXISTS      equ 19
SYSCALL_GUI_PUTS    equ 22
SYSCALL_YIELD       equ 28
SYSCALL_KEY_AVAIL   equ 32

CMD_LINE_MAX  equ 512
CUR_DIR_MAX   equ 256
TEMP_BUF_SIZE equ 4096

section .text

start:
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8

    mov rax, SYSCALL_CREATE_TERM
    lea rdi, [rel title_str]
    mov rsi, 30
    mov rdx, 30
    mov r10, 700
    mov r8, 400
    syscall
    mov [rel cmd_win], eax

    mov byte [rel current_dir], '\'
    mov byte [rel current_dir + 1], 0

    lea rdi, [rel banner_str]
    call print_str
    call print_crlf

main_loop:
    call print_prompt
    lea rdi, [rel cmd_line]
    call read_line
    jc main_loop

    lea rsi, [rel cmd_line]
    call parse_and_exec
    jmp main_loop

; ============================================================
; print_str: print null-terminated string at rdi to our terminal window
; ============================================================
print_str:
    mov rax, SYSCALL_GUI_PUTS
    mov rsi, rdi
    mov edi, [rel cmd_win]
    syscall
    ret

; ============================================================
; print_crlf: print CR+LF
; ============================================================
print_crlf:
    push rdi
    lea rdi, [rel crlf_str]
    call print_str
    pop rdi
    ret

; ============================================================
; print_space: print a single space
; ============================================================
print_space:
    push rdi
    lea rdi, [rel space_str]
    call print_str
    pop rdi
    ret

; ============================================================
; print_prompt: build and print C:\dir> prompt
; ============================================================
print_prompt:
    push rdi
    push rsi
    push rax
    push rcx

    lea rdi, [rel prompt_buf]
    mov byte [rdi], 'C'
    mov byte [rdi + 1], ':'

    lea rsi, [rel current_dir]
    lea rdi, [rel prompt_buf + 2]

.copy_dir:
    mov al, [rsi]
    test al, al
    jz .dir_done
    mov [rdi], al
    inc rsi
    inc rdi
    jmp .copy_dir

.dir_done:
    mov byte [rdi], '>'
    mov byte [rdi + 1], ' '
    mov byte [rdi + 2], 0

    lea rdi, [rel prompt_buf]
    call print_str

    pop rcx
    pop rax
    pop rsi
    pop rdi
    ret

; ============================================================
; read_line: read keyboard input into buffer at rdi
; returns: carry clear if non-empty, set if empty
; ============================================================
read_line:
    push rbx
    push r12
    mov r12, rdi
    xor ebx, ebx

.rl_loop:
    mov rax, SYSCALL_KEY_AVAIL
    syscall
    test rax, rax
    jnz .rl_have_key
    mov rax, SYSCALL_YIELD
    syscall
    jmp .rl_loop

.rl_have_key:
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
    cmp ebx, CMD_LINE_MAX - 2
    jae .rl_loop

    mov [r12 + rbx], al
    inc ebx

    push rdi
    lea rdi, [rel echobuf]
    mov byte [rdi], al
    mov byte [rdi + 1], 0
    call print_str
    pop rdi
    jmp .rl_loop

.rl_bs:
    cmp ebx, 0
    je .rl_loop
    dec ebx
    push rdi
    lea rdi, [rel bs_str]
    call print_str
    pop rdi
    jmp .rl_loop

.rl_done:
    mov byte [r12 + rbx], 0
    call print_crlf
    test ebx, ebx
    jz .rl_empty
    clc
    pop r12
    pop rbx
    ret

.rl_empty:
    stc
    pop r12
    pop rbx
    ret

; ============================================================
; skip_spaces: advance rsi past spaces/tabs, return in rax
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
; strcmp_upper: compare strings at rsi, rdi, uppercase both
; returns: ZF set if equal (rax=0), clobbers rsi, rdi
; ============================================================
strcmp_upper:
    push rsi
    push rdi
.loop:
    mov al, [rsi]
    mov cl, [rdi]
    call .toupper_al
    xchg al, cl
    call .toupper_al
    cmp al, cl
    jne .done
    test al, al
    jz .done
    inc rsi
    inc rdi
    jmp .loop
.done:
    mov rax, 0
    sete al
    dec rax
    and rax, 1
    neg rax
    pop rdi
    pop rsi
    ret

.toupper_al:
    cmp al, 'a'
    jb .toupper_done
    cmp al, 'z'
    ja .toupper_done
    sub al, 32
.toupper_done:
    ret

; ============================================================
; strcmp: exact string compare rsi vs rdi
; returns: ZF set if equal
; ============================================================
strcmp:
    push rsi
    push rdi
.loop:
    mov al, [rsi]
    cmp al, [rdi]
    jne .done
    test al, al
    jz .done
    inc rsi
    inc rdi
    jmp .loop
.done:
    pop rdi
    pop rsi
    ret

; ============================================================
; strlen: get length of string at rdi -> rax
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
; strcpy: copy string from rsi to rdi
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
; parse_and_exec: parse command at rsi, dispatch
; ============================================================
parse_and_exec:
    push rbx
    push r12
    push r13
    push r14
    push r15

    call skip_spaces
    mov rsi, rax
    cmp byte [rsi], 0
    je .done

    ; Split command and args
    lea r12, [rel cmd_buf]     ; r12 = lowercase cmd
    lea r13, [rel args_buf]     ; r13 = args
    xor ecx, ecx
    xor r14d, r14d

.parse_cmd:
    mov al, [rsi]
    test al, al
    jz .cmd_done
    cmp al, ' '
    je .cmd_split
    cmp al, 9
    je .cmd_split
    cmp cl, 63
    jae .cmd_split
    ; Store uppercase
    cmp al, 'a'
    jb .store_cmd
    cmp al, 'z'
    ja .store_cmd
    sub al, 32
.store_cmd:
    mov [r12 + rcx], al
    inc ecx
    inc rsi
    jmp .parse_cmd

.cmd_split:
    mov byte [r12 + rcx], 0
    inc rsi
    call skip_spaces
    mov rsi, rax
    ; Copy rest as args
    lea rdi, [rel args_buf]
    call strcpy
    jmp .have_cmd

.cmd_done:
    mov byte [r12 + rcx], 0
    lea rdi, [rel args_buf]
    mov byte [rdi], 0

.have_cmd:
    lea rsi, [rel cmd_buf]

    ; Dispatch
    lea rdi, [rel exit_str]
    call strcmp
    jz .do_exit

    lea rdi, [rel quit_str]
    call strcmp
    jz .do_exit

    lea rdi, [rel echo_str]
    call strcmp
    jz .do_echo

    lea rdi, [rel cd_str]
    call strcmp
    jz .do_cd

    lea rdi, [rel chdir_str]
    call strcmp
    jz .do_cd

    lea rdi, [rel dir_str]
    call strcmp
    jz .do_dir

    lea rdi, [rel cls_str]
    call strcmp
    jz .do_cls

    lea rdi, [rel type_str]
    call strcmp
    jz .do_type

    lea rdi, [rel ver_str]
    call strcmp
    jz .do_ver

    lea rdi, [rel help_str]
    call strcmp
    jz .do_help

    lea rdi, [rel vol_str]
    call strcmp
    jz .do_vol

    lea rdi, [rel date_str]
    call strcmp
    jz .do_date

    lea rdi, [rel time_str]
    call strcmp
    jz .do_time

    lea rdi, [rel pause_str]
    call strcmp
    jz .do_pause

    lea rdi, [rel mem_str]
    call strcmp
    jz .do_mem

    ; Try external
    lea rdi, [rel cmd_buf]
    lea rsi, [rel args_buf]
    call run_external
    test rax, rax
    jz .done

    lea rdi, [rel not_found_str]
    call print_str
    lea rdi, [rel cmd_buf]
    call print_str
    lea rdi, [rel crlf_str]
    call print_str
    jmp .done

.do_exit:
    xor edi, edi
    mov rax, SYSCALL_EXIT
    syscall

.do_echo:
    lea rdi, [rel args_buf]
    cmp byte [rdi], 0
    je .echo_blank
    call print_str
    call print_crlf
    jmp .done
.echo_blank:
    call print_crlf
    jmp .done

.do_cd:
    lea rdi, [rel args_buf]
    cmp byte [rdi], 0
    jne .cd_set
    ; Print current directory
    lea rdi, [rel cd_display]
    mov byte [rdi], 'C'
    mov byte [rdi + 1], ':'
    lea rsi, [rel current_dir]
    lea rdi, [rel cd_display + 2]
    call strcpy
    lea rdi, [rel cd_display]
    call print_str
    call print_crlf
    jmp .done

.cd_set:
    lea rdi, [rel args_buf]
    cmp byte [rdi], '.'
    jne .cd_not_dot
    cmp byte [rdi + 1], 0
    jne .cd_not_dot
    ; cd . → print current dir
    lea rdi, [rel cd_display]
    mov byte [rdi], 'C'
    mov byte [rdi + 1], ':'
    lea rsi, [rel current_dir]
    lea rdi, [rel cd_display + 2]
    call strcpy
    lea rdi, [rel cd_display]
    call print_str
    call print_crlf
    jmp .done

.cd_not_dot:
    ; Build new path
    lea rsi, [rel args_buf]
    lea rdi, [rel new_dir_buf]
    cmp byte [rsi], '\'
    je .cd_abs
    cmp byte [rsi], '/'
    je .cd_abs
    ; Relative: copy current_dir
    lea rsi, [rel current_dir]
    call strcpy
    lea rdi, [rel new_dir_buf]
    call strlen
    lea rdi, [rel new_dir_buf]
    add rdi, rax
    cmp rax, 1
    je .cd_no_backslash
    cmp byte [rdi - 1], '\'
    je .cd_no_backslash
    mov byte [rdi], '\'
    inc rdi
.cd_no_backslash:
    lea rsi, [rel args_buf]
    call strcpy
    jmp .cd_check_dotdot

.cd_abs:
    lea rsi, [rel args_buf]
    lea rdi, [rel new_dir_buf]
    call strcpy

.cd_check_dotdot:
    ; Handle .. at end
    lea rdi, [rel new_dir_buf]
    call strlen
    cmp rax, 2
    jb .cd_check_exists
    lea rdi, [rel new_dir_buf]
    add rdi, rax
    sub rdi, 2
    cmp byte [rdi], '.'
    jne .cd_check_exists
    cmp byte [rdi + 1], '.'
    jne .cd_check_exists
    ; Remove .. and go up
    cmp rax, 2
    jbe .cd_root
    mov byte [rdi], 0
    dec rdi
    cmp rdi, [rel new_dir_buf]
    jb .cd_root
    ; Find previous backslash
.find_up:
    cmp rdi, [rel new_dir_buf]
    jbe .cd_root
    cmp byte [rdi], '\'
    je .cd_found
    dec rdi
    jmp .find_up
.cd_found:
    mov byte [rdi], 0
    cmp rdi, [rel new_dir_buf]
    jne .cd_check_exists
    mov byte [rdi + 1], 0
    jmp .cd_check_exists
.cd_root:
    mov byte [rel new_dir_buf], '\'
    mov byte [rel new_dir_buf + 1], 0

.cd_check_exists:
    lea rdi, [rel new_dir_buf]
    mov rax, SYSCALL_EXISTS
    syscall
    test rax, rax
    jnz .cd_ok
    lea rdi, [rel cd_bad_path]
    call print_str
    jmp .done

.cd_ok:
    lea rsi, [rel new_dir_buf]
    lea rdi, [rel current_dir]
    call strcpy
    jmp .done

.do_dir:
    ; Determine which path to list
    lea rdi, [rel args_buf]
    cmp byte [rdi], 0
    jne .dir_resolve
    ; No args → use current_dir
    lea rsi, [rel current_dir]
    lea rdi, [rel dir_path_buf]
    call strcpy
    jmp .dir_do_read

.dir_resolve:
    lea rsi, [rel args_buf]
    lea rdi, [rel dir_path_buf]
    cmp byte [rsi], '\'
    je .dir_cpy
    cmp byte [rsi], '/'
    je .dir_cpy
    ; Relative: current_dir + '\' + args
    lea rsi, [rel current_dir]
    call strcpy
    lea rdi, [rel dir_path_buf]
    call strlen
    lea rdi, [rel dir_path_buf]
    add rdi, rax
    cmp rax, 1
    je .dir_rel_nos
    cmp byte [rdi - 1], '\'
    je .dir_rel_nos
    mov byte [rdi], '\'
    inc rdi
.dir_rel_nos:
    lea rsi, [rel args_buf]
    call strcpy
    jmp .dir_do_read

.dir_cpy:
    lea rsi, [rel args_buf]
    lea rdi, [rel dir_path_buf]
    call strcpy

.dir_do_read:
    mov rax, SYSCALL_READDIR
    lea rsi, [rel temp_buf]
    mov rdx, TEMP_BUF_SIZE
    syscall
    cmp rax, 0
    jl .dir_err
    je .dir_empty

    mov r15, rax

    lea rdi, [rel dir_header1]
    call print_str
    lea rdi, [rel dir_path_buf]
    call print_str
    call print_crlf
    call print_crlf

    lea r14, [rel temp_buf]
    xor r12d, r12d
    xor r13d, r13d

.dir_loop:
    lea rax, [rel temp_buf]
    cmp r14, rax
    jb .dir_done_loop
    mov al, [r14]
    test al, al
    jz .dir_done_loop
    lea rax, [rel temp_buf]
    mov rbx, r14
    sub rbx, rax
    cmp rbx, r15
    jae .dir_done_loop

    push r14
    add r14, 2
    call strlen
    mov rcx, rax
    pop r14

    push r14
    lea r14, [r14 + 2 + rcx + 1]
    call strlen
    mov rbx, rax
    pop r14

    push r14
    cmp byte [r14], 'D'
    jne .dir_file

    ; Directory
    inc r13d
    lea rdi, [rel dir_line_buf]
    mov dword [rdi], '    '
    lea rsi, [r14 + 2]
    lea rdi, [rel dir_line_buf + 4]
    call strcpy
    lea rdi, [rel dir_line_buf + 4]
    call strlen
    lea rdi, [rel dir_line_buf + 4]
    add rdi, rax
    mov byte [rdi], '\'
    mov byte [rdi + 1], 0

    lea rdi, [rel dir_line_buf]
    call print_str
    call print_crlf
    pop r14
    add r14, rcx
    add r14, rbx
    add r14, 4
    jmp .dir_loop

.dir_file:
    inc r12d
    lea rdi, [rel dir_line_buf]
    mov dword [rdi], '    '
    lea rsi, [r14 + 2]
    lea rdi, [rel dir_line_buf + 4]
    call strcpy
    lea rdi, [rel dir_line_buf]
    call print_str
    call print_crlf
    pop r14
    add r14, rcx
    add r14, rbx
    add r14, 4
    jmp .dir_loop

.dir_done_loop:
    call print_crlf

    lea rdi, [rel dir_files_str]
    call print_str
    mov rdi, r12
    lea rsi, [rel num_buf]
    call num_to_str
    lea rdi, [rel num_buf]
    call print_str
    lea rdi, [rel dir_files_end]
    call print_str

    lea rdi, [rel dir_dirs_str]
    call print_str
    mov rdi, r13
    lea rsi, [rel num_buf]
    call num_to_str
    lea rdi, [rel num_buf]
    call print_str
    call print_crlf
    jmp .done

.dir_empty:
    lea rdi, [rel dir_empty_str]
    call print_str
    call print_crlf
    jmp .done

.dir_err:
    lea rdi, [rel dir_err_str]
    call print_str
    jmp .done

.do_cls:
    mov rax, SYSCALL_CLR_TERM
    syscall
    jmp .done

.do_type:
    lea rdi, [rel args_buf]
    cmp byte [rdi], 0
    je .type_err

    ; Build full path
    lea rsi, [rdi]
    lea rdi, [rel type_path_buf]
    cmp byte [rsi], '\'
    je .type_abs
    cmp byte [rsi], '/'
    je .type_abs
    lea rsi, [rel current_dir]
    call strcpy
    lea rdi, [rel type_path_buf]
    call strlen
    lea rdi, [rel type_path_buf]
    add rdi, rax
    cmp byte [rdi - 1], '\'
    je .type_no_slash
    mov byte [rdi], '\'
    inc rdi
.type_no_slash:
    lea rsi, [rel args_buf]
    call strcpy
    jmp .type_open
.type_abs:
    lea rsi, [rel args_buf]
    lea rdi, [rel type_path_buf]
    call strcpy

.type_open:
    mov rax, SYSCALL_OPEN
    lea rdi, [rel type_path_buf]
    xor esi, esi
    syscall
    cmp rax, 0
    jl .type_not_found
    mov r12, rax

.type_read_loop:
    mov rax, SYSCALL_READ
    mov rdi, r12
    lea rsi, [rel temp_buf]
    mov rdx, TEMP_BUF_SIZE
    syscall
    cmp rax, 0
    jle .type_close

    ; Print what we read
    lea rdi, [rel temp_buf]
    add rdi, rax
    mov byte [rdi], 0
    lea rdi, [rel temp_buf]
    call print_str
    jmp .type_read_loop

.type_close:
    mov rax, SYSCALL_CLOSE
    mov rdi, r12
    syscall
    call print_crlf
    jmp .done

.type_not_found:
    lea rdi, [rel type_err_str]
    call print_str
    jmp .done

.type_err:
    lea rdi, [rel type_syntax_str]
    call print_str
    jmp .done

.do_ver:
    lea rdi, [rel ver_str_text]
    call print_str
    jmp .done

.do_help:
    lea rdi, [rel help_text]
    call print_str
    jmp .done

.do_vol:
    lea rdi, [rel vol_str_text]
    call print_str
    jmp .done

.do_date:
    lea rdi, [rel date_str_text]
    call print_str
    jmp .done

.do_time:
    lea rdi, [rel time_str_text]
    call print_str
    jmp .done

.do_pause:
    lea rdi, [rel pause_str_text]
    call print_str
    call wait_key
    call print_crlf
    jmp .done

.do_mem:
    lea rdi, [rel mem_str_text]
    call print_str
    jmp .done

.done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

wait_key:
    mov rax, SYSCALL_KEY_AVAIL
    syscall
    test rax, rax
    jnz .have_key
    mov rax, SYSCALL_YIELD
    syscall
    jmp wait_key
.have_key:
    mov rax, SYSCALL_GETCHAR
    syscall
    ret

; ============================================================
; run_external: try to execute cmd (rdi) with args (rsi)
; returns: rax=0 if executed, 1 if not found
; ============================================================
run_external:
    push rbx
    push r12
    push r13

    mov r12, rdi        ; cmd
    mov r13, rsi        ; args (unused for now)

    ; Check if has extension
    xor ebx, ebx        ; 0 = no ext, 1 = has ext

    ; Try current directory
    lea rdi, [rel ext_path_buf]
    lea rsi, [rel current_dir]
    call strcpy
    lea rdi, [rel ext_path_buf]
    call strlen
    lea rdi, [rel ext_path_buf]
    add rdi, rax
    cmp byte [rdi - 1], '\'
    je .ext_no_slash1
    mov byte [rdi], '\'
    inc rdi
.ext_no_slash1:
    lea rsi, [r12]
    call strcpy

    ; Append .exe
    lea rdi, [rel ext_path_buf]
    call strlen
    lea rdi, [rel ext_path_buf]
    add rdi, rax
    mov byte [rdi], '.'
    mov byte [rdi + 1], 'e'
    mov byte [rdi + 2], 'x'
    mov byte [rdi + 3], 'e'
    mov byte [rdi + 4], 0

    mov rax, SYSCALL_PE_CHECK
    lea rdi, [rel ext_path_buf]
    syscall
    test rax, rax
    jnz .ext_exec

    ; Try \SYSTEM\
    lea rdi, [rel ext_path_buf]
    mov byte [rdi], '\'
    mov byte [rdi + 1], 'S'
    mov byte [rdi + 2], 'Y'
    mov byte [rdi + 3], 'S'
    mov byte [rdi + 4], 'T'
    mov byte [rdi + 5], 'E'
    mov byte [rdi + 6], 'M'
    mov byte [rdi + 7], '\'
    lea rsi, [r12]
    lea rdi, [rel ext_path_buf + 8]
    call strcpy

    ; Append .exe
    lea rdi, [rel ext_path_buf]
    call strlen
    lea rdi, [rel ext_path_buf]
    add rdi, rax
    mov byte [rdi], '.'
    mov byte [rdi + 1], 'e'
    mov byte [rdi + 2], 'x'
    mov byte [rdi + 3], 'e'
    mov byte [rdi + 4], 0

    mov rax, SYSCALL_PE_CHECK
    lea rdi, [rel ext_path_buf]
    syscall
    test rax, rax
    jnz .ext_exec

    ; Try \SYSTEM\PROGRAMS\
    lea rdi, [rel ext_path_buf]
    mov byte [rdi], '\'
    mov byte [rdi + 1], 'S'
    mov byte [rdi + 2], 'Y'
    mov byte [rdi + 3], 'S'
    mov byte [rdi + 4], 'T'
    mov byte [rdi + 5], 'E'
    mov byte [rdi + 6], 'M'
    mov byte [rdi + 7], '\'
    mov byte [rdi + 8], 'P'
    mov byte [rdi + 9], 'R'
    mov byte [rdi + 10], 'O'
    mov byte [rdi + 11], 'G'
    mov byte [rdi + 12], 'R'
    mov byte [rdi + 13], 'A'
    mov byte [rdi + 14], 'M'
    mov byte [rdi + 15], 'S'
    mov byte [rdi + 16], '\'
    lea rsi, [r12]
    lea rdi, [rel ext_path_buf + 17]
    call strcpy

    lea rdi, [rel ext_path_buf]
    call strlen
    lea rdi, [rel ext_path_buf]
    add rdi, rax
    mov byte [rdi], '.'
    mov byte [rdi + 1], 'e'
    mov byte [rdi + 2], 'x'
    mov byte [rdi + 3], 'e'
    mov byte [rdi + 4], 0

    mov rax, SYSCALL_PE_CHECK
    lea rdi, [rel ext_path_buf]
    syscall
    test rax, rax
    jnz .ext_exec

    ; Not found
    mov rax, 1
    pop r13
    pop r12
    pop rbx
    ret

.ext_exec:
    lea rdi, [rel ext_path_buf]
    mov rax, SYSCALL_EXEC_WAIT
    syscall

    xor eax, eax
    pop r13
    pop r12
    pop rbx
    ret

; ============================================================
; num_to_str: convert uint rdi to string at rsi
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
; Data
; ============================================================
align 8

title_str:    db "Command Prompt", 0
banner_str:   db "BlueOS Command Interpreter v1.0", 13, 10, "Type 'help' for commands.", 13, 10, 0
exit_str:     db "EXIT", 0
quit_str:     db "QUIT", 0
echo_str:     db "ECHO", 0
cd_str:       db "CD", 0
chdir_str:    db "CHDIR", 0
dir_str:      db "DIR", 0
cls_str:      db "CLS", 0
type_str:     db "TYPE", 0
ver_str:      db "VER", 0
help_str:     db "HELP", 0
vol_str:      db "VOL", 0
date_str:     db "DATE", 0
time_str:     db "TIME", 0
pause_str:    db "PAUSE", 0
mem_str:      db "MEM", 0

crlf_str:     db 13, 10, 0
space_str:    db ' ', 0
bs_str:       db 8, ' ', 8, 0

not_found_str: db "'", 0
cd_bad_path:   db "The system cannot find the path specified.", 13, 10, 0
dir_header1:   db " Directory of ", 0
dir_empty_str: db " File Not Found", 0
dir_err_str:   db " Cannot read directory", 0
dir_files_str: db "        ", 0
dir_files_end: db " File(s)", 0
dir_dirs_str:  db "               ", 0
type_err_str:  db "The system cannot find the file specified.", 13, 10, 0
type_syntax_str: db "The syntax of the command is incorrect.", 13, 10, 0

ver_str_text:  db 13, 10, "BlueOS x86_64 version 1.0", 13, 10, 0
vol_str_text:  db " Volume in drive C is BLUEOS", 13, 10, 0
date_str_text: db " The current date is: 01/01/2026", 13, 10, 0
time_str_text: db " The current time is: 12:00:00", 13, 10, 0
pause_str_text: db "Press any key to continue . . . ", 0
mem_str_text:  db 13, 10, " Memory information not available from user space.", 13, 10, 0

help_text:
db 13, 10
db "BlueOS Command Interpreter - Available commands:", 13, 10
db "================================================", 13, 10
db 13, 10
db "  CD      Change current directory", 13, 10
db "  CLS     Clear the screen", 13, 10
db "  DIR     List directory contents", 13, 10
db "  ECHO    Display messages", 13, 10
db "  EXIT    Quit the command interpreter", 13, 10
db "  HELP    Show this help", 13, 10
db "  PAUSE   Wait for a key press", 13, 10
db "  TIME    Display the system time", 13, 10
db "  TYPE    Display file contents", 13, 10
db "  VER     Display version information", 13, 10
db "  VOL     Display volume label", 13, 10
db 13, 10
db "  <prog>  Run an executable (.exe from PATH)", 13, 10
db 13, 10, 0

align 8
prompt_buf:    times 64 db 0
cmd_line:      times CMD_LINE_MAX db 0
cmd_buf:       times 64 db 0
args_buf:      times CMD_LINE_MAX db 0
current_dir:   times CUR_DIR_MAX db 0
new_dir_buf:   times CUR_DIR_MAX db 0
dir_path_buf:  times CUR_DIR_MAX db 0
type_path_buf: times CUR_DIR_MAX db 0
ext_path_buf:  times CUR_DIR_MAX db 0
temp_buf:      times TEMP_BUF_SIZE db 0
num_buf:       times 32 db 0
dir_line_buf:  times 128 db 0
cd_display:    times CUR_DIR_MAX + 4 db 0
cmd_win:       dd 0
echobuf:       db 0, 0
