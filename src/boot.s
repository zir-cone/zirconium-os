/* src/boot.s */

.set MULTIBOOT_MAGIC,    0x1BADB002
.set MULTIBOOT_FLAGS,    0x00000000
.set MULTIBOOT_CHECKSUM, -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

/* Multiboot header: must be in a loadable segment, in the first 8 KiB */
    .section .multiboot
    .align 4
    .long MULTIBOOT_MAGIC
    .long MULTIBOOT_FLAGS
    .long MULTIBOOT_CHECKSUM

    .section .text
    .globl _start
    .extern kernel_main

_start:
    cli

    /* set up stack */
    mov $stack_top, %esp

    /* call C++ kernel entry */
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

    .section .bss
    .align 16
stack_bottom:
    .skip 16384        /* 16 KiB stack */
stack_top:
