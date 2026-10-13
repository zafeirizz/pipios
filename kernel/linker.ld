/* AuroraOS kernel linker script - flat binary linked at 1 MiB */
ENTRY(_start)
SECTIONS
{
    . = 0x100000;
    __kernel_start = .;

    .text : {
        *(.text*)
    }
    . = ALIGN(16);
    .rodata : {
        *(.rodata*)
    }
    . = ALIGN(16);
    .data : {
        *(.data*)
    }
    . = ALIGN(4096);
    __bss_start = .;
    .bss : {
        *(COMMON)
        *(.bss*)
    }
    . = ALIGN(16);
    __bss_end = .;
    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
