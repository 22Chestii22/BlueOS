BITS 32

section .multiboot2_header
align 8
header_start:
    dd 0xE85250D6
    dd 0
    dd header_end - header_start
    dd 0x100000000 - (0xE85250D6 + 0 + (header_end - header_start))

    align 8
    dw 5
    dw 0
    dd 20
    dd 1280
    dd 720
    dd 32
header_end:

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

align 4096
pml4:
    resb 4096
pdpt:
    resb 4096
pd:
    resb 4096

extern kernel_main

section .text
global _start
_start:
    mov esp, stack_top
    mov edi, ebx

    mov eax, pml4
    mov cr3, eax

    mov eax, pdpt
    or eax, 1
    mov [pml4], eax

    mov eax, pd
    or eax, 1
    mov [pdpt], eax

    xor ecx, ecx
.map_pd_32:
    mov eax, 0x200000
    mul ecx
    or eax, 0x83
    mov [pd + ecx*8], eax
    inc ecx
    cmp ecx, 32
    jb .map_pd_32

    mov eax, cr4
    or eax, 1 << 5
    or eax, 1 << 7
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    lgdt [gdt64_ptr]
    jmp 0x08:_start64

BITS 64
_start64:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, stack_top

    ; BSS already zeroed by GRUB — do NOT zero again,
    ; it would wipe out our page tables (pml4/pdpt/pd)
    ; which live in .bss!

    mov rdi, rbx
    call kernel_main

    cli
halt_loop:
    hlt
    jmp halt_loop

section .rodata
gdt64:
    dq 0
    dq 0x0020980000000000
    dq 0x0000920000000000
gdt64_end:

gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dq gdt64
