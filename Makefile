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

$(BUILD)/timer.o: kernel/timer.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/task.o: kernel/task.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/disk.o: kernel/disk.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/fs.o: kernel/fs.c
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
	$(BUILD)/timer.o \
	$(BUILD)/shell.o \
	$(BUILD)/task.o \
	$(BUILD)/disk.o \
	$(BUILD)/fs.o

	$(LD) $(LDFLAGS) -o $@ \
	$(BUILD)/kernel_entry.o \
	$(BUILD)/interrupts.o \
	$(BUILD)/kernel.o \
	$(BUILD)/idt.o \
	$(BUILD)/io.o \
	$(BUILD)/pic.o \
	$(BUILD)/keyboard.o \
	$(BUILD)/mm.o \
	$(BUILD)/shell.o \
	$(BUILD)/timer.o \
	$(BUILD)/task.o \
	$(BUILD)/disk.o \
	$(BUILD)/fs.o

# ---- Combine bootloader + kernel into a single disk image ----
$(BUILD)/rinkos.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	cat $(BUILD)/boot.bin $(BUILD)/kernel.bin > $@
	# Pad up to an 8MB image.
	# boot.asm now loads the kernel and addresses the filesystem purely
	# via LBA (INT13h AH=0x42, extended read) rather than CHS, so we're
	# no longer pinned to a floppy-sized image for geometry reasons.
	# Quite the opposite, in fact: a 1.44MB image makes QEMU/SeaBIOS
	# synthesize a floppy-like CHS geometry for the emulated IDE disk,
	# and extended reads that cross those synthetic cylinder boundaries
	# were observed to fail/hang. An 8MB image gets a normal hard-disk
	# geometry and has plenty of room for FS_VOL_START_LBA in
	# kernel/scr/fs.h to grow further later too.
	truncate -s '>8388608' $@

# ---- Run in QEMU ----
# Boot as a floppy (-fda), not a hard disk (-drive format=raw). Floppy
# geometry is fixed (80 cylinders, 2 heads, 18 sectors/track) so the
# simple CHS reads in boot.asm behave predictably.
run: $(BUILD)/rinkos.img
	qemu-system-i386 -drive file=$(BUILD)/rinkos.img,format=raw,if=ide

debug: $(BUILD)/rinkos.img
	qemu-system-i386 -d in_asm,int -D log.txt -drive file=$(BUILD)/rinkos.img,format=raw,if=ide

clean:
	rm -rf $(BUILD)

.PHONY: all run clean
