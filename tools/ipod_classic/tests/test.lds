/*
 * Link the bare-metal tests to run from SDRAM, where the machine's -kernel
 * path drops the image and where the firmware directory of a real .ipsw says
 * the osos image loads (addr = 0x08000000).
 */

ENTRY(_start)

SECTIONS
{
    . = 0x08000000;

    .text : {
        *(.text.start)
        *(.text*)
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        . = ALIGN(8);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.comment)
        *(.note*)
    }
}
