// src/kernel.cpp
#include <stdint.h>
#include <stddef.h>
#include "interrupts.h"
#include "keyboard.h"
#include "console.h"
#include "kernel_module.h"
#include "../forty-fs/ffs.h"
#include "../clam/clamshell/clamshell.h"

namespace {
    void kernel_log(const char* message) {
        console_write(message);
    }

    bool init_interrupts() {
        idt_init();
        kernel_log("IDT installed.\nPIC remapped.\n");
        return true;
    }

    bool init_storage() {
        if (!ffs::init()) {
            kernel_log("FFS initialization failed.\n");
            return false;
        }
        kernel_log("FFS initialized.\n");
        return true;
    }

    bool init_shell() {
        clamshell::init();
        return true;
    }
}

extern "C" void kernel_main() {
    console_initialize();
    console_write("ZirconiumOS kernel starting...\n");

    static const kernel_module kModules[] = {
        {
            "interrupts",
            1u,
            KERNEL_MODULE_API_CONSOLE,
            KERNEL_MODULE_API_INTERRUPTS,
            init_interrupts,
        },
        {
            "storage",
            1u,
            KERNEL_MODULE_API_CONSOLE | KERNEL_MODULE_API_INTERRUPTS,
            KERNEL_MODULE_API_STORAGE,
            init_storage,
        },
        {
            "shell",
            1u,
            KERNEL_MOPDULE_API_CONSOLE | KERNEL_MODULE_API_STORAGE,
            KERNEL_MODULE_API_SHELL,
            init_shell,
        },
    };

    uint32_t ready_mask = KERNEL_MODULE_API_CONSOLE;
    kernel_modules_init(kModules, sizeof(kModules) / sizeof(kModules[0]), kernel_log, &ready_mask);
    // is that.... SIX SEVEN?!?!?!?!?!?!!!?????? 😱😱😱😱🔥‼️‼️‼️🔥🔥🚨🚨🚨🔥🔥‼️‼️🗣️🗣️🗣️🧯💯💯💯🥀🥀🥀🥀🥀🥀🥀🥀
    asm volatile("sti"); // enable interrupts
    console_write("Keyboard enabled.\n");
    console_write("\n> ");

    clamshell::repl();
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
