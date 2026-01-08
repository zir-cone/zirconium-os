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
<<<<<<< HEAD
#endif
=======
#endif
>>>>>>> 6f01370f08b307819c9bd57b453eedf4d2977a6e
