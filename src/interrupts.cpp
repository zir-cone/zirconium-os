// src/interrupts.cpp
#include <stdint.h>
#include <stddef.h>
#include "ports.h"
#include "interrupts.h"

extern "C" void idt_flush(uint32_t);  // in interrupts.s
extern "C" void irq1_stub();          // in interrupts.s

struct idt_entry 
{
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr 
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static idt_entry idt[256];
static idt_ptr   idtp;

static void *k_memset(void *dst, int value, size_t size) 
{
    unsigned char *p = (unsigned char*)dst;
    for (size_t i = 0; i < size; ++i) {
        p[i] = (unsigned char)value;
    }
    return dst;
}

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) 
{
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel       = sel;
    idt[num].always0   = 0;
    idt[num].flags     = flags;
}

static void pic_remap() 
{
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20); // master
    outb(0xA1, 0x28); // slave
    outb(0x21, 0x04); // tell master about slave at IRQ2
    outb(0xA1, 0x02); // tell slave its cascade identity
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    // mask all IRQs except keyboard (IRQ1 on master).
    //
    // PIC mask bits: 1 = masked (disabled), 0 = unmasked (enabled)
    // master PIC IRQs: bit 0 = IRQ0 (timer), bit 1 = IRQ1 (keyboard), et cetera.
    //
    // 0xFD = 1111 1101b:
    // -- bit 1 = 0  --> IRQ1 enabled
    // -- all others = 1 --> disabled

    outb(0x21, 0xFD);  // master IRQ1
    outb(0xA1, 0xFF);  // slave
}

void idt_init() 
{
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;

    k_memset(&idt, 0, sizeof(idt));

    // Get the current code segment selector by grub(hub)
    uint16_t cs;
    asm volatile ("mov %%cs, %0" : "=r"(cs)); // these ALWAYS have red squigglies and ITS PISSING ME THE FUCK OFF

    // IRQ1 after remap is interrupt vector 0x21
    idt_set_gate(0x21, (uint32_t)irq1_stub, cs, 0x8E);


    idt_flush((uint32_t)&idtp);
    pic_remap();
}