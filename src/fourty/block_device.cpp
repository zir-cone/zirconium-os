// src/fourty/block_device.cpp
#include "block_device.h"

bool bd_init() {
    // implement real disk init here (ATA, AHCI, etc)
    // for now just pretend failure
    return false;
}

bool bd_read_block(uint32_t, void*) {
    return false;
}

bool bd_write_block(uint32_t, const void*) {
    return false;
}