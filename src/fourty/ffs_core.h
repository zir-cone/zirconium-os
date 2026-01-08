#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void ffs_memzero(void* dst, uint64_t bytes);
void ffs_memcpy(void* dst, const void* src, uint64_t bytes);

#ifdef __cplusplus
}
#endif
