// src/console.h
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void console_initialize();
void console_write(const char* str);
void console_putc(char c);

#ifdef __cplusplus
}
#endif