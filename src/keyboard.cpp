// src/keyboard.cpp
#include <stdint.h>
#include "ports.h"
#include "console.h"

// Last character pressed; EATEN by keyboard_get_last_char()
static volatile char g_last_char = 0;

// Track Shift state
static bool g_shift_pressed = false;

// US QWERTY scancode set 1 keymaps
// Index = scancode
static const char keymap[128] = {
    0,  27,                             // 0x00, 0x01 (Esc)
    '1','2','3','4','5','6','7','8','9','0','-','=', // 0x02–0x0D
    '\b',                               // 0x0E Backspace
    '\t',                               // 0x0F Tab
    'q','w','e','r','t','y','u','i','o','p','[',']', // 0x10–0x1B
    '\n',                               // 0x1C Enter
    0,                                  // 0x1D Control (ignored)
    'a','s','d','f','g','h','j','k','l',';','\'','`', // 0x1E–0x29
    0,                                  // 0x2A Left Shift
    '\\',                               // 0x2B
    'z','x','c','v','b','n','m',',','.','/', // 0x2C–0x35
    0,                                  // 0x36 Right Shift
    '*',                                // 0x37 (Keypad *)
    0,                                  // 0x38 Alt
    ' ',                                // 0x39 Space
    // rest 0

};

static const char keymap_shift[128] = {
    0,  27,                             // 0x00, 0x01
    '!','@','#','$','%','^','&','*','(',')','_','+', // 0x02–0x0D
    '\b',                               // 0x0E Backspace
    '\t',                               // 0x0F Tab
    'Q','W','E','R','T','Y','U','I','O','P','{','}', // 0x10–0x1B
    '\n',                               // 0x1C Enter
    0,                                  // 0x1D Control
    'A','S','D','F','G','H','J','K','L',':','"','~', // 0x1E–0x29
    0,                                  // 0x2A Left Shift
    '|',                                // 0x2B
    'Z','X','C','V','B','N','M','<','>','?', // 0x2C–0x35
    0,                                  // 0x36 Right Shift
    '*',                                // 0x37
    0,                                  // 0x38 Alt
    ' ',                                // 0x39 Space
    // rest 0
};

extern "C" void keyboard_handler() {
    uint8_t scancode = inb(0x60);

    // Key release?
    if (scancode & 0x80) {
        uint8_t code = scancode & 0x7F;
        // Shift release
        if (code == 0x2A || code == 0x36) {
            g_shift_pressed = false;
        }
        // nothing else to do
    } else {
        // Key press
        if (scancode == 0x2A || scancode == 0x36) {
            g_shift_pressed = true;
        } else {
            char ch = g_shift_pressed ? keymap_shift[scancode] : keymap[scancode];
            if (ch != 0) {
                g_last_char = ch;
            }
        }
    }

    // Send EOI to master PIC
    outb(0x20, 0x20);
}

// Clam polls this function in read_line()
char keyboard_get_last_char() {
    char c = g_last_char;
    g_last_char = 0;      // consume
    return c;
}
