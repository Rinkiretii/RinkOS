; ============================================================
; RinkOS - Stage 1 Bootloader
; Loaded by the BIOS at 0x7C00, runs in 16-bit real mode.
; Job: print a message, load the kernel from disk, switch to
; 32-bit protected mode, and jump into the kernel.
; ============================================================
 
BITS 16
ORG 0x7C00

KERNEL_OFFSET equ 0x1000      ; where we load the kernel in memory

start:
    cli                       ; disable interrupts while we set things up
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00            ; stack grows down from the boot sector
    sti

    mov [BOOT_DRIVE], dl      ; BIOS passes the boot drive number in dl

    mov si, MSG_REAL_MODE
    call print_string

    call detect_memory
    call load_kernel
    call switch_to_pm

    jmp $                     ; safety net, should never hit this

; ------------------------------------------------------------
; print_string: prints a null-terminated string via BIOS teletype
; input: SI = pointer to string
; ------------------------------------------------------------
print_string:
    pusha
    mov ah, 0x0E
.loop:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .loop
.done:
    popa
    ret

; ------------------------------------------------------------
; disk_load: loads DH sectors from drive DL into ES:BX
; ------------------------------------------------------------
disk_load:
    push dx

    mov ah, 0x02              ; BIOS read sectors function
    mov al, dh                ; number of sectors to read
    mov ch, 0x00               ; cylinder 0
    mov dh, 0x00               ; head 0
    mov cl, 0x02               ; start reading from sector 2 (sector 1 = boot sector)

    int 0x13
    jc disk_error              ; carry flag set => error

    pop dx
    cmp al, dh                 ; BIOS sets AL = sectors actually read
    jne disk_error
    ret

disk_error:
    mov si, MSG_DISK_ERROR
    call print_string
    jmp $

load_kernel:
    mov si, MSG_LOAD_KERNEL
    call print_string

    mov bx, KERNEL_OFFSET      ; ES:BX = where to load the kernel
    mov dh, 32                 ; number of sectors to read (adjust to kernel size)
    mov dl, [BOOT_DRIVE]
    call disk_load
    ret

; ------------------------------------------------------------
; detect_memory: uses BIOS int 0x15, eax=0xE820 to build a
; memory map. Stores entries at MEMORY_MAP_ADDR, and the entry
; count at MEMORY_MAP_COUNT.
; ------------------------------------------------------------
MEMORY_MAP_ADDR   equ 0x9000    ; safely below 0x90000 stack, unused otherwise
MEMORY_MAP_COUNT  equ 0x8FFC    ; 4 bytes just before the map, holds count

detect_memory:
    pusha

    mov di, MEMORY_MAP_ADDR
    xor ebx, ebx                ; continuation value, 0 to start
    xor bp, bp                  ; entry counter

    mov edx, 0x534D4150          ; "SMAP" magic
.loop:
    mov eax, 0xE820
    mov ecx, 24                  ; ask for 24-byte entries
    int 0x15
    jc .done                     ; carry set = unsupported / done

    cmp eax, 0x534D4150           ; BIOS should echo SMAP back in eax
    jne .done

    test ecx, ecx                 ; skip zero-length entries
    je .skip

    inc bp
    add di, 24

.skip:
    test ebx, ebx                 ; ebx = 0 means this was the last entry
    je .done

    cmp bp, 64                    ; cap at 64 entries, plenty for now
    jl .loop

.done:
    mov [MEMORY_MAP_COUNT], bp
    popa
    ret

; ------------------------------------------------------------
; Switch to 32-bit protected mode
; ------------------------------------------------------------
switch_to_pm:
    cli                        ; BIOS interrupts are useless in PM
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x1                ; set the PE (protection enable) bit
    mov cr0, eax

    jmp CODE_SEG:init_pm        ; far jump to flush the CPU pipeline

BITS 32
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000
    mov esp, ebp

    call KERNEL_OFFSET          ; jump into the kernel entry point
    jmp $                        ; should never return

; ------------------------------------------------------------
; Global Descriptor Table
; ------------------------------------------------------------
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

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; ------------------------------------------------------------
; Data
; ------------------------------------------------------------
BOOT_DRIVE      db 0
MSG_REAL_MODE   db "RinkOS: booting (16-bit real mode)...", 13, 10, 0
MSG_LOAD_KERNEL db "RinkOS: loading kernel from disk...", 13, 10, 0
MSG_DISK_ERROR  db "RinkOS: disk read error!", 13, 10, 0

; ------------------------------------------------------------
; Boot sector padding + signature
; ------------------------------------------------------------
times 510-($-$$) db 0
dw 0xAA55
