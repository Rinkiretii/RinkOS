; ============================================================
; RinkOS - Stage 1 Bootloader
; Loaded by the BIOS at 0x7C00, runs in 16-bit real mode.
; Job: print a message, load the kernel from disk, switch to
; 32-bit protected mode, and jump into the kernel.
; ============================================================
 
BITS 16
ORG 0x7C00

KERNEL_OFFSET equ 0x20000     ; linear address where the kernel is loaded and
                               ; runs. Deliberately far above the boot sector
                               ; (0x7C00) - loading straight at 0x1000 meant a
                               ; kernel bigger than ~54 sectors would grow past
                               ; 0x7C00 during the real-mode disk-load loop
                               ; itself and overwrite the boot sector code
                               ; while it was still running (self-modifying
                               ; code, by accident). 0x20000 leaves ~448KB of
                               ; headroom before the next fixed landmark (the
                               ; 0x90000 stack set up in switch_to_pm).
KERNEL_LOAD_SEGMENT equ 0x2000 ; ES value for the real-mode load: segment
                               ; 0x2000 * 16 = 0x20000 linear, since BX alone
                               ; (16-bit) can't reach past 0xFFFF.

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
; disk_load: loads DH sectors (starting at LBA 1, i.e. the sector
; right after the boot sector) from drive DL into ES:BX.
;
; Uses INT 13h AH=0x42 (extended/LBA read), issued in 32-sector
; chunks rather than one big request. Some BIOS/emulator combinations
; misbehave (hang, no error) on a single large extended-read request
; well under the documented ~127-sector packet limit - chunking
; sidesteps whatever that limit actually is here, and also means we
; don't have to reason about the transfer crossing a 64KB real-mode
; segment boundary (handled per-chunk below).
; ------------------------------------------------------------
disk_load:
    pusha

    movzx ecx, dh            ; ecx = total sectors left to read
    mov eax, 1                ; current LBA (1 = right after the boot sector)

.read_loop:
    cmp ecx, 0
    je .read_done

    mov ebp, ecx              ; ebp = this chunk's sector count (NOT edx -
    cmp ebp, 32                ; edx/dl must stay untouched, it holds the
    jbe .chunk_size_ok         ; boot drive number that INT 13h needs on
    mov ebp, 32                ; every call through this loop)
.chunk_size_ok:
    mov [dap_count], bp
    mov [dap_lba_lo], eax
    mov [dap_offset], bx
    mov [dap_segment], es

    mov si, dap
    mov ah, 0x42            ; extended read (LBA)
    int 0x13
    jc disk_error

    add eax, ebp              ; advance to the next LBA

    ; advance the ES:BX buffer pointer by (sectors_read * 512) bytes,
    ; rolling the offset overflow into the segment as needed
    push cx
    mov cx, bp
    shl cx, 9                ; cx = ebp * 512 (max 32*512 = 16384, fits in 16 bits)
    add bx, cx
    jnc .no_wrap
    mov cx, es
    add cx, 0x1000
    mov es, cx
.no_wrap:
    pop cx

    sub ecx, ebp
    jmp .read_loop

.read_done:
    popa
    ret

dap:                        ; Disk Address Packet for INT 13h AH=42h
    db 0x10                 ; packet size
    db 0x00                 ; reserved
dap_count:    dw 0          ; number of sectors to read (this chunk)
dap_offset:   dw 0          ; destination offset (this chunk)
dap_segment:  dw 0          ; destination segment (this chunk)
dap_lba_lo:   dd 0          ; starting LBA (this chunk)
dap_lba_hi:   dd 0

disk_error:
    mov si, MSG_DISK_ERROR
    call print_string
    jmp $


load_kernel:
    mov si, MSG_LOAD_KERNEL
    call print_string

    mov ax, KERNEL_LOAD_SEGMENT
    mov es, ax
    xor bx, bx                  ; ES:BX = KERNEL_LOAD_SEGMENT:0 = 0x20000 linear
    mov dh, 150                 ; number of sectors to read (adjust to kernel
                                 ; size; kept < 300 = FS_VOL_START_LBA in
                                 ; kernel/scr/fs.h, with headroom to spare)
    mov dl, [BOOT_DRIVE]
    call disk_load

    xor ax, ax
    mov es, ax                   ; restore ES=0 for whatever runs next
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
