# ============================================================
# RinkOS build system
#
# Requires: nasm, gcc (with 32-bit support), ld, qemu-system-i386
# On Debian/Ubuntu:
#   sudo apt install nasm gcc-multilib qemu-system-x86
# On Arch:
#   sudo pacman -S nasm gcc qemu-system-x86 --needed
# ============================================================

CC      = gcc 
LD      = ld
ASM     = nasm

CFLAGS  = -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -Wall -Wextra -c
LDFLAGS = -m elf_i386 -T kernel/linker.ld --oformat binary

BUILD   = build

all: $(BUILD)/rinkos.img

# ---- Bootloader (flat binary, 512 bytes) ----
$(BUILD)/boot.bin: boot/boot.asm
	mkdir -p $(BUILD)
	$(ASM) -f bin $< -o $@

# ---- Kernel ASM entry ----
$(BUILD)/kernel_entry.o: kernel/kernel_entry.asm
	mkdir -p $(BUILD)
	$(ASM) -f elf32 $< -o $@

# ---- Interrupt ASM ----
$(BUILD)/interrupts.o: kernel/interrupts.asm
	mkdir -p $(BUILD)
	$(ASM) -f elf32 $< -o $@


# ---- Kernel C files ----
$(BUILD)/kernel.o: kernel/kernel.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/idt.o: kernel/idt.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/io.o: kernel/io.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/pic.o: kernel/pic.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/keyboard.o: kernel/keyboard.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/mm.o: kernel/mm.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/shell.o: kernel/shell.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

# ---- Link kernel entry + kernel.c into one flat binary ----
$(BUILD)/kernel.bin: \
	$(BUILD)/kernel_entry.o \
	$(BUILD)/interrupts.o \
	$(BUILD)/kernel.o \
	$(BUILD)/idt.o \
	$(BUILD)/io.o \
	$(BUILD)/pic.o \
	$(BUILD)/keyboard.o \
	$(BUILD)/mm.o \
	$(BUILD)/shell.o

	$(LD) $(LDFLAGS) -o $@ \
	$(BUILD)/kernel_entry.o \
	$(BUILD)/interrupts.o \
	$(BUILD)/kernel.o \
	$(BUILD)/idt.o \
	$(BUILD)/io.o \
	$(BUILD)/pic.o \
	$(BUILD)/keyboard.o \
	$(BUILD)/mm.o \
	$(BUILD)/shell.o

# ---- Combine bootloader + kernel into a single disk image ----
$(BUILD)/rinkos.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	cat $(BUILD)/boot.bin $(BUILD)/kernel.bin > $@
	# Pad up to a standard 1.44MB floppy image size (1474560 bytes).
	# This matters for two reasons:
	#  1. boot.asm reads sectors 2-16 unconditionally, so the image
	#     must actually contain that many sectors or the BIOS read fails.
	#  2. Booting a tiny raw image as a hard disk makes QEMU guess a
	#     CHS geometry from the file size, which usually does NOT match
	#     the fixed cylinder-0/head-0 assumptions in boot.asm. A full
	#     floppy-sized image sidesteps that guesswork entirely.
	truncate -s '>1474560' $@

# ---- Run in QEMU ----
# Boot as a floppy (-fda), not a hard disk (-drive format=raw). Floppy
# geometry is fixed (80 cylinders, 2 heads, 18 sectors/track) so the
# simple CHS reads in boot.asm behave predictably.
run: $(BUILD)/rinkos.img
	qemu-system-i386 -fda $(BUILD)/rinkos.img

debug: $(BUILD)/rinkos.img
	qemu-system-i386 -d in_asm,int -D log.txt -fda $(BUILD)/rinkos.img

clean:
	rm -rf $(BUILD)

.PHONY: all run clean
