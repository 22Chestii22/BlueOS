OUTPUT_FORMAT(elf64-x86-64)
ENTRY(module_entry)

SECTIONS
{
    . = 0;
    .text : ALIGN(16) { *(.text*) }
    .rodata : ALIGN(16) { *(.rodata*) }
    .data : ALIGN(16) { *(.data*) }
    .bss : ALIGN(16) { *(COMMON) *(.bss*) }
}
