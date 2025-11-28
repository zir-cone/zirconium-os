// src/keyboard.cpp
#include <stdint.h>
#include "ports.h"
#include "keyboard.h"

static volatile char last_char = 0;

// US keyboard layout, scancode set 1 (partial)
static const char keymap[128] = 
{
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',   // 0x00-0x0F
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,    // 0x10-0x1F
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',       // 0x20-0x2C
    'z','x','c','v','b','n','m',',','.','/', 0,   '*', 0,   ' ',     // 0x2D-0x39
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                  // rest unused
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

extern "C" void keyboard_handler();
extern "C" void keyboard_handler() 
{
    uint8_t scancode = inb(0x60);

    // Ignore key release 4 now
    if (scancode < 128) {
        char c = keymap[scancode];
        if (c) {
            last_char = c;
        }
    }

    // Send EOI to master PIC (IRQ1 is on master)
    outb(0x20, 0x20);
}

char keyboard_get_last_char() 
{
    char c = last_char;
    last_char = 0;
    return c;
}
