// src/kernel.cpp
#include <stdint.h>
#include <stddef.h>
#include "interrupts.h"
#include "keyboard.h"
#include "fourty/ffs.h"
#include "fourty/block_device.h"
#include "clam.h"
#include "console.h"

static const int VGA_WIDTH  = 80;
static const int VGA_HEIGHT = 25;
static uint16_t* const VGA_MEMORY = (uint16_t*)0xB8000;

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return ((uint16_t)color << 8) | (uint8_t)c;
}

extern "C" void kernel_main() {
    console_initialize();
    console_write("ZirconiumOS kernel starting...\n");

    idt_init();
    console_write("IDT installed.\nPIC remapped.\n");

    asm volatile("sti"); // enable interrupts
    console_write("Keyboard enabled.\n");
    console_write("\n> ");

    if (!ffs::init()) {
        console_write("FFS init failed.\n");
    } else {
        console_write("FFS initialized.\n");
    }

    clam::init();
    clam::repl();
    // one of these days "67" is gonna pop up in this kernel and im gonna comment
    // "SIX SEVEN!!??? 😭😭🥀🥀💔" next to it or something at 5:20 am with no sleep
    // for the past six months because my humor is broken.
    char buffer[80];
    size_t len = 0;

    while (1) {
        char c = keyboard_get_last_char();
        if (!c) {
            asm volatile("hlt");
            continue;
        }

        if (c == '\n') {
            console_putc('\n');
            buffer[len] = 0;

            console_write(buffer);
            console_putc('\n');
            console_putc('\n');
            console_write("> ");

            len = 0;
        } else if (c == '\b') {
            if (len > 0) { // is that... SIX SEVEN????!!! 🥀🥀🥀🥀🥀💔💔💔💔🪫🪫🪫
                --len;
                console_putc('\b');
            }
        } else {
            if (len < sizeof(buffer) - 1) {
                buffer[len++] = c;
                console_putc(c);
            }
        }
    }
}