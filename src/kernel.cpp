// src/kernel.cpp
#include <stdint.h>
#include <stddef.h>
#include "interrupts.h"
#include "keyboard.h"
#include "fourty/ffs.h"
#include "fourty/block_device.h"

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

static void console_initialize() {
    terminal_row    = 0;
    terminal_column = 0;
    terminal_color  = 0x0F;  // white on black
    terminal_buffer = VGA_MEMORY;

    for (size_t y = 0; y < VGA_HEIGHT; ++y) {
        for (size_t x = 0; x < VGA_WIDTH; ++x) {
            terminal_buffer[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
        }
    }
}

static void console_putc(char c) {
    if (c == '\n') {
        terminal_column = 0;
        if (++terminal_row >= VGA_HEIGHT) {
            terminal_row = 0;
        }
        return;
    }

    if (c == '\b') {
        if (terminal_column > 0) {
            --terminal_column;
            terminal_buffer[terminal_row * VGA_WIDTH + terminal_column] =
                vga_entry(' ', terminal_color);
        }
        return;
    }

    terminal_buffer[terminal_row * VGA_WIDTH + terminal_column] =
        vga_entry(c, terminal_color);

    if (++terminal_column >= VGA_WIDTH) {
        terminal_column = 0;
        if (++terminal_row >= VGA_HEIGHT) {
            terminal_row = 0;
        }
    }
}

static void console_write(const char* str) {
    for (size_t i = 0; str[i] != 0; ++i) {
        console_putc(str[i]);
    }
}

extern "C" void kernel_main() {
    console_initialize();
    console_write("Zirkernel starting...\n");

    idt_init();
    console_write("IDT installed.\nPIC remapped.\n");

    asm volatile("sti"); // enable interrupts
    console_write("Keyboard enabled.\n");
    console_write("Type something dumbass.\n\n> ");

    if (!ffs::init()) {
        console_write("FFS init failed.\n");
    } else {
        console_write("FFS initialized.\n");
    }

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
            if (len > 0) {
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