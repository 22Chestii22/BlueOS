; BlueOS Stage 1 - FAT32 VBR
; Loaded by BIOS at 0x7C00
; Loads stage2 from reserved sectors, jumps to it
; Must fit in 512 bytes with BPB

ORG 0x7C00
BITS 16

; FAT32 BPB (offsets 0x00-0x59)
bpb_jmp:       jmp short stage1_start
               nop
bpb_oem:       db "BLUEOS  "
bpb_bytes_per_sec:  dw 512
bpb_sec_per_cluster: db 1
bpb_reserved_sectors: dw 32
bpb_num_fats:   db 2
bpb_root_entries: dw 0
bpb_total_sectors_16: dw 0
bpb_media:      db 0xF8
bpb_fat_size_16: dw 0
bpb_sec_per_track: dw 63
bpb_num_heads:  dw 16
bpb_hidden:     dd 0
bpb_total_sectors_32: dd 0

; FAT32 extended BPB (offsets 0x24-0x59)
bpb_fat_size_32: dd 0
bpb_ext_flags:  dw 0
bpb_fs_version: dw 0
bpb_root_cluster: dd 2
bpb_fs_info:    dw 1
bpb_backup_boot: dw 6
bpb_reserved:   times 12 db 0
bpb_drive:      db 0x80
bpb_reserved1:  db 0
bpb_ext_sig:    db 0x29
bpb_vol_id:     dd 0
bpb_vol_label:  db "BLUEOS     "
bpb_fs_type:    db "FAT32   "

; --- Boot code starts here ---
stage1_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl

    ; Reset disk
    mov ah, 0
    int 0x13

    ; Check for INT 13h extensions
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .chs_fallback

    ; LBA read: load stage2 from LBA 7 to 0x0600
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    jmp 0x0000:0x0600

.chs_fallback:
    ; INT 13h extensions not available - use CHS
    mov si, err_chs
    call print_str
    hlt
    jmp $-1

disk_error:
    mov si, err_msg
    call print_str
    hlt
    jmp $-1

print_str:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print_str
.done:
    ret

; Data
boot_drive: db 0
err_msg:    db "Boot error!", 0
err_chs:    db "No LBA!", 0

; Disk Address Packet
dap:
    db 0x10
    db 0
    dw 24          ; sectors (12KB)
    dw 0x0600      ; buffer offset
    dw 0           ; buffer segment
    dq 7           ; LBA start

times 510-($-$$) db 0
dw 0xAA55
