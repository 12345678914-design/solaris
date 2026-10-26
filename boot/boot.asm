bits 32
section .multiboot

align 4
    dd 0x1BADB002
    dd 0
    dd -(0x1BADB002)

section .bss
align 16
pila_kernel_inferior:
    resb 131072
pila_kernel_superior:

section .text
global _start
extern kernel_main

_start:
    mov esp, pila_kernel_superior
    xor ebp, ebp
    call kernel_main

hang:
    cli
    hlt
    jmp hang
