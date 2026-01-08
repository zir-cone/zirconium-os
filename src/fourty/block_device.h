// src/fourty/block_device.h

#pragma once
#include <stdint.h>

<<<<<<< HEAD
// Block size is fixed at 4096 bytes for FFS 1.0
=======
// Block size is fixed at 4096 bytes for FFS 2.0
>>>>>>> 6f01370f08b307819c9bd57b453eedf4d2977a6e
static const uint32_t BD_BLOCK_SIZE = 4096;
// init block deive (eg detect ATA disk, etc)
bool bd_init();
// read 1 block (LBA) into buffer (must be at least BD_BLOCK_SIZE bytes)
bool bd_read_block(uint32_t lba, void* buffer);
// write 1 block (LBA) from buffer
<<<<<<< HEAD
bool bd_write_block(uint32_t lba, const void* buffer);
=======
bool bd_write_block(uint32_t lba, const void* buffer);
>>>>>>> 6f01370f08b307819c9bd57b453eedf4d2977a6e
