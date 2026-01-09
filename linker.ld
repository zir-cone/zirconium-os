/* linker.ld */

ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)

SECTIONS
{
    /* Load kernel at 1 MiB */
    . = 1M;

    /* Put multiboot header at the very start of the text segment */
    .text :
    {
        *(.multiboot)
        *(.text*)
    }

    .rodata :
    {
        *(.rodata*)
    }

    .data :
    {
        *(.data*)
    }

    .bss :
    {
        *(.bss*)
        *(COMMON)
    }
}
