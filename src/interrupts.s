/* src/interrupts.s */
.section .text
.global idt_flush
.global irq1_stub
.extern keyboard_handler

# void idt_flush(uint32_t idt_ptr);
idt_flush:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

# IRQ1 stub:
irq1_stub:
    pusha                  # save registers
    call keyboard_handler  # call C++ handler
    popa
    iret                   # return from interrupt
