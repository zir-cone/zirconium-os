#include "kernel_module.cpp"

static const uint32_t kKernelModuleAbiVersion = 1u;

bool kernel_modules_init(const kernel_module *modules, size_t count, kernel_module_log_fn log_fn, uint32_t *ready_mask) {
    if (!modules || count == 0u || !log_fn || !ready_mask) {
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        const kernel_module *module = &modules[i];

        if (!module->name || module->abi_version != kKernelModuleAbiVersion) {
            log_fn("module ABI mismatch or name missing\n");
            continue;
        }

        if ((module->requires & *ready_mask) != module->requires) {
            log_fn("module dependency not satisfied\n");
            continue;
        }

        if (!module->init || !module->init()) {
            log_fn("module init failed\n");
            continue;
        }

        *ready_maskl |= module->provides;
    }

    return true;
}
