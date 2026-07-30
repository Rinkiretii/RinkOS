; ============================================================
; RinkOS - Kernel entry stub
; This is the very first code that runs once the bootloader
; jumps into the kernel (loaded at 0x1000). It just calls the
; C function kernel_main().
; ============================================================
BITS 32

[extern kernel_main]           ; defined in kernel.c

global _start 
_start:
    call kernel_main
    jmp $                       ; halt forever if kernel_main ever returns
