// src/keyboard.cpp
#include <stdint.h>
#include "ports.h"
#include "console.h"

// Ring buffer for keypresses
static volatile char g_keybuf[64];
static volatile uint8_t g_head = 0;
static volatile uint8_t g_tail = 0;

// Track Shift state
static bool g_shift_pressed = false;

// US QWERTY scancode set 1 keymaps
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
};

// Push a char into the ring buffer (from IRQ)
static void kb_push(char c) {
    uint8_t next_head = (g_head + 1) & (64 - 1);
    if (next_head == g_tail) {
        // buffer full, drop key
        return;
    }
    g_keybuf[g_head] = c;
    g_head = next_head;
}

// Pop a char from the ring buffer (from main code)
static char kb_pop() {
    if (g_head == g_tail) {
        return 0;
    }
    char c = g_keybuf[g_tail];
    g_tail = (g_tail + 1) & (64 - 1);
    return c;
}

// IRQ1 handler
extern "C" void keyboard_handler() {
    uint8_t scancode = inb(0x60);

    if (scancode & 0x80) {
        // Key release
        uint8_t code = scancode & 0x7F;
        if (code == 0x2A || code == 0x36) {
            g_shift_pressed = false;
        }
    } else {
        // Key press
        if (scancode == 0x2A || scancode == 0x36) {
            g_shift_pressed = true;
        } else {
            char ch = g_shift_pressed ? keymap_shift[scancode] : keymap[scancode];
            if (ch != 0) {
                kb_push(ch);
            }
        }
    }

    // Send EOI to master PIC
    outb(0x20, 0x20);
}

// Clam polls this
char keyboard_get_last_char() {
    return kb_pop();
}
// fuck me