OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    .multiboot2_header : ALIGN(8)
    {
        KEEP(*(.multiboot2_header))
    }

    .text : ALIGN(4096)
    {
        *(.text*)
        *(.gnu.linkonce.t*)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata*)
        *(.gnu.linkonce.r*)
    }

    .data : ALIGN(4096)
    {
        *(.data*)
        *(.gnu.linkonce.d*)
    }

    .bss : ALIGN(4096)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        *(.gnu.linkonce.b*)
        __bss_end = .;
    }

    __end = .;
}
