# RinkOS

A minimal x86 operating system: a hand-written bootloader in assembly
that loads a small C kernel and drops it into 32-bit protected mode.

## Project layout

```
rinkos/
├── boot/
│   └── boot.asm          # Stage-1 bootloader (16-bit real mode -> 32-bit protected mode)
├── kernel/
|   ├──scr/
|   └── io.h 
│   ├── kernel_entry.asm  # Tiny asm stub that calls kernel_main()
│   ├── kernel.c          # The actual kernel (VGA text output for now)
|   ├── interrupts.asm
|   ├── irq.c
|   ├── keyboard.c
|   ├── pic.c
│   └── linker.ld         # Places the kernel at the address boot.asm jumps to
├── Makefile
└── README.md
```

## How it works

1. The BIOS loads the first 512 bytes of the disk (the boot sector,
   `boot.asm`) to memory address `0x7C00` and jumps there.
2. `boot.asm` prints a status message, then uses BIOS interrupt
   `0x13` to read the kernel's sectors off disk into memory at
   `0x1000`.
3. It builds a Global Descriptor Table (GDT), sets the PE bit in
   `CR0`, and far-jumps into 32-bit protected mode.
4. In protected mode it calls straight into `0x1000`, which is
   `kernel_entry.asm`'s `_start` — this just calls the C function
   `kernel_main()`.
5. `kernel.c` writes directly to the VGA text buffer at `0xB8000`
   to print to the screen, then halts the CPU in a loop.

## Requirements

Install on Debian/Ubuntu:

```bash
sudo apt install nasm gcc-multilib qemu-system-x86
```

Arch:

```bash
sudo pacman -S nasm gcc qemu-system-x86 --needed
```

If you use the `i686-elf-*` toolchain, update `CC`/`LD` at the top
of the `Makefile` accordingly and drop the `-m32` flag (that
toolchain is already 32-bit-only by default).

## Build and run

```bash
make        # builds build/rinkos.img
make run    # boots it in QEMU
```

You should see:

```
RinkOS: booting (16-bit real mode)...
RinkOS: loading kernel from disk...
RinkOS kernel loaded successfully.
Welcome to RinkOS!
--------------------------------
Kernel is running in 32-bit protected mode.
```

## Notes / gotchas

- **Kernel size vs. sectors loaded**: `boot.asm` currently reads
  `15` sectors (`mov dh, 15`) for the kernel. Each sector is 512
  bytes, so that's room for ~7.5 KB of kernel code. If your kernel
  grows past that, bump this number (and know that reading past a
  cylinder boundary needs more elaborate CHS/LBA handling — this
  bootloader keeps things simple and assumes everything fits on the
  first cylinder/head).
- **No 64-bit mode yet**: this boots into 32-bit protected mode.
  Going to long mode (64-bit) is a good next milestone once this
  boots reliably.
- **No paging, no interrupts (IDT), no memory manager yet** — this
  is deliberately the smallest possible skeleton: boot -> protected
  mode -> C code -> print to screen. Natural next steps, roughly in
  order:
  1. Set up a Global Descriptor Table cleanly and an Interrupt
     Descriptor Table (IDT) so you can handle exceptions/IRQs.
  2. Write a keyboard driver (IRQ1) so the kernel can take input.
  3. Add a basic physical memory manager, then paging.
  4. Move to long mode (64-bit) if you want a modern base.
  5. Build a simple shell/command loop on top of the keyboard driver.

## Testing without QEMU installed

If you don't have QEMU, you can also write `build/rinkos.img` to a
USB stick (⚠️ this will erase the drive — double check the device
path) and boot a real machine from it:

```bash
sudo dd if=build/rinkos.img of=/dev/sdX bs=512 status=progress
```
