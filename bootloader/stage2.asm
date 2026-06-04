; BlueOS Stage 2 - Kernel Loader
; Loaded by stage1 at 0x0600
; 16-bit real mode -> 64-bit long mode bootloader
; Parses FAT32 to load KERNEL.ELF, sets up VBE and long mode

ORG 0x0600
BITS 16

; ============================================================
; Entry Point
; ============================================================
stage2_entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    ; Calculate FAT LBA = reserved_sectors
    mov ax, [0x7C00 + 0x0E]
    mov [fat_lba], ax
    mov word [fat_lba + 2], 0

    ; Calculate data LBA = reserved_sectors + num_fats * fat_size_32
    movzx bx, byte [0x7C00 + 0x10]
    mov eax, [0x7C00 + 0x24]
    mul bx
    add eax, [fat_lba]
    mov [data_lba], eax

    ; Step 1: Find and load KERNEL.ELF
    call fat_find_kernel
    jc err_no_kernel
    call fat_load_file
    jc err_load_fail

    ; Step 2: Set up VBE framebuffer
    call vbe_init
    jc err_vbe

    ; Step 3: Get memory map
    call e820_probe

    ; Step 4: Build boot info
    call bi_build

    ; Step 5: Enable A20
    call a20_enable
    jc err_a20

    ; Step 6: Build page tables
    call pt_build

    ; Step 7: Set up GDT
    call gdt_setup

    ; Step 8: Enter long mode (never returns)
    call enter_lm
    hlt

err_no_kernel: mov si, s_no_kernel; call print16; hlt; jmp $-1
err_load_fail: mov si, s_load_fail; call print16; hlt; jmp $-1
err_vbe:       mov si, s_vbe_fail;  call print16; hlt; jmp $-1
err_a20:       mov si, s_a20_fail;  call print16; hlt; jmp $-1

print16:
    lodsb
    or al, al
    jz .d
    mov ah, 0x0E
    int 0x10
    jmp print16
.d: ret

; ============================================================
; Disk read: LBA in EAX, buffer ES:BX
; ============================================================
disk_read:
    push si
    mov [dap_lba], eax
    mov word [dap_buf], bx
    mov word [dap_buf+2], es
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    pop si
    ret

; ============================================================
; FAT32: Read cluster chain entry from FAT
; Input: EAX = cluster, Output: EAX = next cluster
; ============================================================
fat_next:
    push bx
    push cx
    push dx
    push si
    mov ebx, eax
    shl ebx, 2
    mov eax, ebx
    shr eax, 9
    add eax, [fat_lba]
    and bx, 0x1FF
    push bx
    mov bx, 0x1000
    call disk_read
    pop bx
    jc .err
    mov eax, [0x1000 + ebx]
    and eax, 0x0FFFFFFF
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.err:
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; ============================================================
; FAT32: Find KERNEL.ELF in root directory
; ============================================================
fat_find_kernel:
    push bx
    push cx
    push dx
    push si
    push di

    mov eax, [0x7C00 + 0x2C]
    mov [cur_clust], eax

.dir_loop:
    mov eax, [cur_clust]
    call clust_to_lba
    jc .err

    mov di, 0x1000
    movzx ecx, byte [0x7C00 + 0x0D]

.sector_loop:
    push cx
    push eax
    mov bx, di
    call disk_read
    pop eax
    jc .next_clust_pop

    mov si, di
    mov cx, 16

.entry_loop:
    cmp byte [si], 0x00
    je .not_found
    cmp byte [si], 0xE5
    je .next
    cmp byte [si + 11], 0x0F
    je .next

    push si
    push di
    push cx
    mov di, si
    mov si, kernel_name
    mov cx, 11
    cld
    repe cmpsb
    pop cx
    pop di
    pop si
    je .found

.next:
    add si, 32
    dec cx
    jnz .entry_loop

    inc eax
    add di, 512
    pop cx
    dec cx
    jnz .sector_loop
    jmp .next_clust

.next_clust_pop:
    pop cx
.next_clust:
    mov eax, [cur_clust]
    call fat_next
    mov [cur_clust], eax
    cmp eax, 0x0FFFFFF8
    jae .not_found
    cmp eax, 0x0FFFFFF7
    je .err
    test eax, eax
    jz .err
    jmp .dir_loop

.found:
    mov eax, [si + 0x1C]
    mov [file_size], eax
    movzx eax, word [si + 0x14]
    shl eax, 16
    movzx ebx, word [si + 0x1A]
    or eax, ebx
    mov [file_clust], eax

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret

.not_found:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

.err:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; ============================================================
; Convert cluster to LBA: eax = data_lba + (cluster-2) * spc
; ============================================================
clust_to_lba:
    push ebx
    sub eax, 2
    movzx ebx, byte [0x7C00 + 0x0D]
    mul ebx
    add eax, [data_lba]
    pop ebx
    ret

; ============================================================
; FAT32: Load kernel to 0x10000 (segment 0x1000)
; ============================================================
fat_load_file:
    push eax
    push bx
    push cx
    push di

    mov eax, [file_clust]
    mov [cur_clust], eax
    xor di, di

.load_loop:
    mov eax, [cur_clust]
    test eax, eax
    jz .done
    cmp eax, 0x0FFFFFF8
    jae .done
    cmp eax, 0x0FFFFFF7
    je .err

    call clust_to_lba
    movzx ecx, byte [0x7C00 + 0x0D]

.read_loop:
    push cx
    push eax
    push di
    mov bx, di
    push 0x1000
    pop es
    call disk_read
    pop di
    pop eax
    pop cx
    jc .err
    inc eax
    add di, 512
    dec cx
    jnz .read_loop

    mov eax, [cur_clust]
    call fat_next
    mov [cur_clust], eax
    jmp .load_loop

.done:
    pop di
    pop cx
    pop bx
    pop eax
    clc
    ret

.err:
    pop di
    pop cx
    pop bx
    pop eax
    stc
    ret

; ============================================================
; VBE: Find and set framebuffer mode
; Tries 1920x1080, 1280x720, 1024x768, all 32bpp
; ============================================================
vbe_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    ; Get VBE controller info
    mov ax, 0x4F00
    mov di, boot_info         ; reuse boot_info area at 0xC000 as VBE buffer
    int 0x10
    cmp ax, 0x004F
    jne vbe_fail

    cmp dword [boot_info], 'VBE2'
    je vbe_scan
    cmp dword [boot_info], 'VESA'
    jne vbe_fail

vbe_scan:
    ; Get mode list pointer (offset 0x0E = segment, 0x10 = offset)
    mov ax, [boot_info + 0x0E]
    mov [vbe_ml_seg], ax
    mov ax, [boot_info + 0x10]
    mov [vbe_ml_off], ax

    ; Try each preferred mode
    mov si, vbe_pref
vbe_try:
    mov cx, [si]
    cmp cx, 0xFFFF
    je vbe_fallback
    add si, 2

    mov ax, 0x4F01
    mov di, vbe_modeinfo
    int 0x10
    cmp ax, 0x004F
    jne vbe_try

    test byte [vbe_modeinfo], 0x90
    jz vbe_try
    cmp byte [vbe_modeinfo + 0x19], 32
    jne vbe_try

    ; Check width/height match expected
    mov ax, [vbe_modeinfo + 0x12]
    cmp ax, [si - 2]
    jne vbe_try
    mov ax, [vbe_modeinfo + 0x14]
    cmp ax, [si]
    jne vbe_try

vbe_set:
    mov [vbe_width], ax
    mov ax, [vbe_modeinfo + 0x12]
    mov [vbe_width], ax
    mov ax, [vbe_modeinfo + 0x14]
    mov [vbe_height], ax
    mov ax, [vbe_modeinfo + 0x10]
    mov [vbe_pitch], ax
    mov eax, [vbe_modeinfo + 0x28]
    mov [vbe_fb], eax
    mov al, [vbe_modeinfo + 0x19]
    mov [vbe_bpp], al

    mov ax, 0x4F02
    mov bx, cx
    or bx, 0x4000
    int 0x10
    cmp ax, 0x004F
    jne vbe_fail

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

vbe_fallback:
    ; Iterate all modes, find first 32bpp >= 1024 wide
    mov si, [vbe_ml_off]
    push 0
    pop fs
    ; FS segment loaded from vbe_ml_seg
    push word [vbe_ml_seg]
    pop fs

vbe_fb_loop:
    mov cx, [fs:si]
    cmp cx, 0xFFFF
    je vbe_fail

    push si
    mov ax, 0x4F01
    mov di, vbe_modeinfo
    int 0x10
    pop si

    cmp ax, 0x004F
    jne vbe_fb_skip

    test byte [vbe_modeinfo], 0x90
    jz vbe_fb_skip
    cmp byte [vbe_modeinfo + 0x19], 32
    jne vbe_fb_skip
    cmp word [vbe_modeinfo + 0x12], 1024
    jb vbe_fb_skip

    jmp vbe_set

vbe_fb_skip:
    add si, 2
    jmp vbe_fb_loop

vbe_fail:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; ============================================================
; E820: Probe memory map, store at 0xE000
; ============================================================
e820_probe:
    pushad

    xor ebx, ebx
    mov di, 0xE000
    xor cx, cx

.e820_loop:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150
    int 0x15
    jc .e820_done

    inc cx
    add di, 24
    test ebx, ebx
    jnz .e820_loop

.e820_done:
    mov [mmap_entries], cx
    popad
    ret

; ============================================================
; Boot Info: Build structure at boot_info (0xC000)
; ============================================================
bi_build:
    pushad

    ; offset 0: magic
    mov dword [boot_info], 0x554F5342
    mov dword [boot_info + 4], 0x4942

    ; offset 8: fb_addr
    mov eax, [vbe_fb]
    mov [boot_info + 8], eax
    mov dword [boot_info + 12], 0

    ; offset 16: fb_width, fb_height, fb_pitch, fb_bpp
    mov ax, [vbe_width]
    mov [boot_info + 16], ax
    mov ax, [vbe_height]
    mov [boot_info + 20], ax
    mov ax, [vbe_pitch]
    mov [boot_info + 24], ax
    mov al, [vbe_bpp]
    mov [boot_info + 28], al

    ; offset 32: mem_size
    call bi_calc_mem
    mov [boot_info + 32], eax
    mov [boot_info + 36], edx

    ; offset 40: mmap_count
    movzx eax, word [mmap_entries]
    mov [boot_info + 40], eax

    ; offset 48+: copy E820 entries
    movzx ecx, word [mmap_entries]
    jecxz .bi_done
    mov si, 0xE000
    mov di, boot_info + 48

.bi_copy:
    mov eax, [si]
    mov [di], eax
    mov eax, [si+4]
    mov [di+4], eax
    mov eax, [si+8]
    mov [di+8], eax
    mov eax, [si+16]
    mov [di+16], eax
    add si, 24
    add di, 24
    dec cx
    jnz .bi_copy

.bi_done:
    popad
    ret

; ============================================================
; Calculate memory size from E820 map
; Returns EAX=size low, EDX=size high
; ============================================================
bi_calc_mem:
    push cx
    push si
    xor eax, eax
    xor edx, edx
    mov si, 0xE000
    movzx ecx, word [mmap_entries]
    jecxz .cm_done

.cm_loop:
    cmp dword [si + 16], 1
    jne .cm_skip
    mov ebx, [si]
    mov edi, [si + 4]
    mov eax, [si + 8]
    mov edx, [si + 12]
    add eax, ebx
    adc edx, edi
    cmp edx, [bi_mem + 4]
    jb .cm_skip
    ja .cm_set
    cmp eax, [bi_mem]
    jbe .cm_skip
.cm_set:
    mov [bi_mem], eax
    mov [bi_mem + 4], edx
.cm_skip:
    add si, 24
    dec cx
    jnz .cm_loop

.cm_done:
    mov eax, [bi_mem]
    mov edx, [bi_mem + 4]
    pop si
    pop cx
    ret

; ============================================================
; A20: Enable A20 gate
; ============================================================
a20_enable:
    push ax

    in al, 0x92
    test al, 2
    jnz a20_ok
    or al, 2
    and al, 0xFE
    out 0x92, al
    test al, 2
    jnz a20_ok

    cli
    call a20_wait
    mov al, 0xD1
    out 0x64, al
    call a20_wait
    mov al, 0xDF
    out 0x60, al
    call a20_wait
    sti

    push ds
    push es
    xor ax, ax
    mov ds, ax
    mov ax, 0xFFFF
    mov es, ax
    mov ax, [0x7C00]
    cmp ax, [es:0x7C10]
    pop es
    pop ds
    je a20_bad

a20_ok:
    pop ax
    clc
    ret

a20_bad:
    pop ax
    stc
    ret

a20_wait:
    in al, 0x64
    test al, 2
    jnz a20_wait
    ret

; ============================================================
; Page Tables: Build 4-level paging at 0x8000
; Maps first 4GB with 2MB pages
; ============================================================
pt_build:
    pushad

    ; Zero 0x8000-0xBFFF
    push es
    xor ax, ax
    mov es, ax
    mov di, 0x8000
    mov cx, 0x2000
    xor eax, eax
    cld
    rep stosd

    ; PML4[0] = 0x9003
    mov dword [0x8000], 0x9003
    mov dword [0x8004], 0x0000

    ; PDPT[0..7] = 0xA000..0xF000
    mov di, 0x9000
    mov eax, 0xA003
    mov cx, 8
.pt_pdpt:
    mov [di], eax
    mov [di+4], 0
    add eax, 0x1000
    add di, 8
    dec cx
    jnz .pt_pdpt

    ; PD[0] at 0xA000: 512 entries * 2MB = 1GB
    mov di, 0xA000
    mov eax, 0x000083
    mov cx, 512
.pt_pd0:
    mov [di], eax
    mov [di+4], 0
    add eax, 0x200000
    add di, 8
    dec cx
    jnz .pt_pd0

    ; PD[1] at 0xB000: 1GB-2GB
    mov di, 0xB000
    mov cx, 512
.pt_pd1:
    mov [di], eax
    mov [di+4], 0
    add eax, 0x200000
    add di, 8
    dec cx
    jnz .pt_pd1

    pop es
    popad
    ret

; ============================================================
; GDT: 0x00=null, 0x08=32-bit code, 0x10=64-bit code, 0x18=data
; GDT at 0xC800
; ============================================================
gdt_setup:
    pushad

    mov dword [0xC800], 0x00000000
    mov dword [0xC804], 0x00000000
    mov dword [0xC808], 0x0000FFFF
    mov dword [0xC80C], 0x00CF9A00
    mov dword [0xC810], 0x00000000
    mov dword [0xC814], 0x00209800
    mov dword [0xC818], 0x0000FFFF
    mov dword [0xC81C], 0x00CF9200

    mov word [gdt_ptr], 31
    mov dword [gdt_ptr + 2], 0xC800

    popad
    ret

; ============================================================
; Enter Long Mode
; 16-bit real -> 32-bit protected -> 64-bit long mode
; Does NOT return
; ============================================================
enter_lm:
    cli
    lgdt [gdt_ptr]

    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

BITS 32
pm_entry:
    mov ax, 0x18
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov eax, 0x8000
    mov cr3, eax

    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr

    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    jmp 0x10:lm_entry

BITS 64
lm_entry:
    mov ax, 0x18
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, 0x7C00

    ; Parse and load ELF segments from 0x100000 (buffer at 0x100000)
    call elf_load

    ; Jump to kernel entry with boot_info in RDI
    mov rdi, boot_info
    mov rax, [abs kernel_entry]
    call rax

.halt: hlt; jmp .halt

; ============================================================
; ELF64 Loader
; Parses ELF at 0x100000, loads segments to target vaddrs
; ============================================================
elf_load:
    ; Check ELF magic
    cmp dword [abs 0x100000], 0x464C457F
    jne elf_fail
    cmp byte [abs 0x100004], 2
    jne elf_fail

    ; Read entry point
    mov rax, [abs 0x100018]
    mov [abs kernel_entry], rax

    ; Read program header offset and count
    mov r8, [abs 0x100020]         ; phoff
    movzx r9, word [abs 0x100038]  ; phnum
    movzx r10, word [abs 0x100036] ; phdr_sz

    xor r11, r11
    add r8, 0x100000

.elf_loop:
    cmp r11, r9
    jae .elf_done

    cmp dword [r8], 1
    jne .elf_next

    mov rax, [r8 + 0x08]      ; p_offset
    mov rbx, [r8 + 0x10]      ; p_vaddr
    mov rcx, [r8 + 0x20]      ; p_filesz
    mov rdx, [r8 + 0x28]      ; p_memsz

    test rcx, rcx
    jz .elf_zero

    mov rsi, 0x100000
    add rsi, rax
    mov rdi, rbx
    rep movsb

.elf_zero:
    mov rax, [r8 + 0x20]
    mov rbx, [r8 + 0x28]
    sub rbx, rax
    jle .elf_next
    mov rdi, [r8 + 0x10]
    add rdi, rax
    xor al, al
    mov rcx, rbx
    rep stosb

.elf_next:
    add r8, r10
    inc r11
    jmp .elf_loop

.elf_done:
    ret

elf_fail:
    hlt
    jmp $-1

; ============================================================
; Data Section
; ============================================================
boot_drive:    db 0
zero_seg:      dw 0
kernel_name:   db "KERNEL  ELF"

; FAT32
fat_lba:       dd 0
data_lba:      dd 0
cur_clust:     dd 0
file_clust:    dd 0
file_size:     dd 0

; VBE
vbe_fb:        dd 0
vbe_width:     dw 0
vbe_height:    dw 0
vbe_pitch:     dw 0
vbe_bpp:       db 0
vbe_ml_seg:    dw 0
vbe_ml_off:    dw 0
vbe_pref:      dw 0x121, 3840, 2160   ; 4K (Bochs/QEMU VBE)
               dw 0x11D, 1920, 1080
               dw 0x11E, 1280, 720
               dw 0x117, 1280, 768
               dw 0x118, 1024, 768
               dw 0xFFFF

; E820
mmap_entries:  dw 0
bi_mem:        dd 0, 0

; GDT pointer
gdt_ptr:       dw 0
               dd 0
               dw 0

; Kernel entry (64-bit)
kernel_entry:  dq 0

; Disk Address Packet
dap:           db 0x10
               db 0
               dw 1
dap_buf:       dw 0, 0
dap_lba:       dq 0

; Boot info structure at 0xC000
boot_info      equ 0xC000
vbe_modeinfo   equ 0xD000

; Error messages
s_no_kernel:   db "KERNEL.ELF not found", 0
s_load_fail:   db "Load failed", 0
s_vbe_fail:    db "VBE failed", 0
s_a20_fail:    db "A20 failed", 0
