/* src/boot.s */

.set MULTIBOOT_MAGIC, 0x1BADB002
.set MULTIBOOT_FLAGS, 0x0
.set MULTIBOOT_CHECKSUM, -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

.section .MULTIBOOT_CHECKSUM
    .align 4
    .long MULTIBOOT_MAGIC
    .long MULTIBOOT_FLAGS
    .long MULTIBOOT_CHECKSUM

.section .bss
    .align 16
stack_bottom:
    .skip 16385 /* 16 KB stack */
stack_top:

.section .text
    .global start

start:
    mov $stack_top, %esp /* set stack pointer */

    call kernel_main /* call into C++ */

hang:
    cli
    hlt
    jmp hang /* jmp or hang urself */