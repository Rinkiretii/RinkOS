ENTRY(_start)

SECTIONS
{
    /* Must match KERNEL_OFFSET in boot.asm */
    . = 0x1000;

    .text : ALIGN(4)
    {
        *(.text)
    }

    .rodata : ALIGN(4)
    {
        *(.rodata*)
    }

    .data : ALIGN(4)
    {
        *(.data)
    }

    .bss : ALIGN(4)
    {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        _bss_end = .;
    }
}
