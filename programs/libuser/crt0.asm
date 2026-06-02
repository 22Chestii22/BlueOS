BITS 64

extern main
extern exit

global _start
section .text._start


_start:
    ; PE32+ entry point
    sub rsp, 8 ; align stack
    xor edi, edi ; argc = 0
    xor esi, esi ; argv = NULL
    call main
    mov edi, eax
    call exit
