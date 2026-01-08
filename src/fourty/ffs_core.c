#include "ffs_core.h"

#ifdef __x86_64__
extern void ffs_memzero_x64(void* dst, uint64_t bytes);
#endif

void ffs_memzero(void* dst, uint64_t bytes) {
#ifdef __x86_64__
    ffs_memzero_x64(dst, bytes);
#else
    uint8_t* d = (uint8_t*)dst;
    for (uint64_t i = 0; i < bytes; ++i) {
        d[i] = 0;
    }
#endif
}

void ffs_memcpy(void* dst, const void* src, uint64_t bytes) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint64_t i = 0; i < bytes; ++i) {
        d[i] = s[i];
    }
<<<<<<< HEAD
}
=======
}
>>>>>>> 6f01370f08b307819c9bd57b453eedf4d2977a6e
