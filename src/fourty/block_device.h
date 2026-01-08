// src/fourty/block_device.h

#pragma once
#include <stdint.h>

// Block size is fixed at 4096 bytes for FFS 1.0
static const uint32_t BD_BLOCK_SIZE = 4096;
// init block deive (eg detect ATA disk, etc)
bool bd_init();
// read 1 block (LBA) into buffer (must be at least BD_BLOCK_SIZE bytes)
bool bd_read_block(uint32_t lba, void* buffer);
// write 1 block (LBA) from buffer
bool bd_write_block(uint32_t lba, const void* buffer);