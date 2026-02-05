#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*kernel_module_log_fn)(const char *message);
typedef bool (*kernel_module_init_fn)();

typedef struct kernel_module {

    const char *name;
    uint32_t abi_version;
    uint32_t requires;
    uint32_t provides;
    kernel_module_init_fn init;

} kernel_module;

enum {
    KERNEL_MODULE_API_CONSOLE       = 1u << 0,
    KERNEL_MODULE_API_INTERRUPTS    = 1u << 1,
    KERNEL_MODULE_API_STORAGE       = 1u << 2,
    KERNEL_MODULE_API_SHELL         = 1u << 3,
};

bool kernel_modules_init(const kernel_module *modules, size_t count, kernel_module_log_fn log_fn, uint32_t *ready_mask);

#ifdef __cplusplus
}
#endif
