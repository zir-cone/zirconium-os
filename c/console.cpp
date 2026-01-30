// src/console.cpp
#include "console.h"
#include <stdint.h>

static const uint16_t VGA_WIDTH  = 80;
static const uint16_t VGA_HEIGHT = 25;
static uint16_t* const VGA_MEMORY = (uint16_t*)0xB8000;

static uint8_t  g_console_color = 0x07; // light grey on black
static uint16_t g_console_row   = 0;
static uint16_t g_console_col   = 0;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

static void console_put_entry_at(char c, uint8_t color, uint16_t x, uint16_t y) {
    const uint16_t index = y * VGA_WIDTH + x;
    VGA_MEMORY[index] = vga_entry(c, color);
}

static void console_clear() {
    for (uint16_t y = 0; y < VGA_HEIGHT; ++y) {
        for (uint16_t x = 0; x < VGA_WIDTH; ++x) {
            console_put_entry_at(' ', g_console_color, x, y);
        }
    }
    g_console_row = 0;
    g_console_col = 0;
}

void console_initialize() {
    g_console_color = 0x07; // light grey on black
    console_clear();
}

void console_putc(char c) {
    if (c == '\n') {
        g_console_col = 0;
        if (++g_console_row >= VGA_HEIGHT) {
            // scroll by one line
            for (uint16_t y = 1; y < VGA_HEIGHT; ++y) {
                for (uint16_t x = 0; x < VGA_WIDTH; ++x) {
                    VGA_MEMORY[(y - 1) * VGA_WIDTH + x] =
                        VGA_MEMORY[y * VGA_WIDTH + x];
                }
            }
            // clear last line
            for (uint16_t x = 0; x < VGA_WIDTH; ++x) {
                console_put_entry_at(' ', g_console_color, x, VGA_HEIGHT - 1);
            }
            g_console_row = VGA_HEIGHT - 1;
        }
        return;
    }

    if (c == '\r') {
        g_console_col = 0;
        return;
    }

    if (c == '\b') {
        if (g_console_col > 0) {
            --g_console_col;
            console_put_entry_at(' ', g_console_color, g_console_col, g_console_row);
        }
        return;
    }

    console_put_entry_at(c, g_console_color, g_console_col, g_console_row);
    if (++g_console_col >= VGA_WIDTH) {
        g_console_col = 0;
        if (++g_console_row >= VGA_HEIGHT) {
            // scroll
            for (uint16_t y = 1; y < VGA_HEIGHT; ++y) {
                for (uint16_t x = 0; x < VGA_WIDTH; ++x) {
                    VGA_MEMORY[(y - 1) * VGA_WIDTH + x] =
                        VGA_MEMORY[y * VGA_WIDTH + x];
                }
            }
            for (uint16_t x = 0; x < VGA_WIDTH; ++x) {
                console_put_entry_at(' ', g_console_color, x, VGA_HEIGHT - 1);
            }
            g_console_row = VGA_HEIGHT - 1;
        }
    }
}

void console_write(const char* str) {
    if (!str) return;
    while (*str) {
        console_putc(*str++);
    }
}
