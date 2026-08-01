; ============================================================
; RinkOS - Kernel entry stub
; This is the very first code that runs once the bootloader
; jumps into the kernel (loaded at 0x1000). It just calls the
; C function kernel_main().
; ============================================================
BITS 32

[extern kernel_main]           ; defined in kernel.c
[extern _bss_start]
[extern _bss_end]

global _start 
_start:
    ; zero .bss
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    call kernel_main
    jmp $                       ; halt forever if kernel_main ever returns
