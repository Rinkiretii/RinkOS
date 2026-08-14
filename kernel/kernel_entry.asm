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

section .text
_start:
    lgdt [gdt_descriptor]       ; stop depending on the bootloader's GDT

    ; zero .bss
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    call kernel_main
    jmp $                       ; halt forever if kernel_main ever returns

; ------------------------------------------------------------
; Kernel-owned GDT.
;
; boot.asm sets up its own GDT, but that table physically lives
; inside the boot sector at 0x7C00-0x7DFF - memory the BIOS loaded
; it into and that nothing ever reclaims. As the kernel grows, its
; zeroed .bss region can extend past 0x7C00 and stomp on that
; leftover GDT (this is exactly what happened once the filesystem
; driver grew past ~27KB). Loading our own copy here, stored in
; .data (so it ships with kernel.bin ahead of .bss and is never
; touched by the .bss zero loop above), makes the kernel independent
; of the bootloader's memory once it's running.
;
; Selectors match boot.asm's table exactly (0x08 code / 0x10 data)
; so the CS/DS/etc already loaded by the bootloader stay valid -
; no far jump needed, just lgdt.
; ------------------------------------------------------------
section .data
gdt_start:
gdt_null:
    dd 0x0
    dd 0x0

gdt_code:                        ; flat code segment, base 0, limit 4GB
    dw 0xFFFF
    dw 0x0
    db 0x0
    db 10011010b
    db 11001111b
    db 0x0

gdt_data:                        ; flat data segment, base 0, limit 4GB
    dw 0xFFFF
    dw 0x0
    db 0x0
    db 10010010b
    db 11001111b
    db 0x0

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start


