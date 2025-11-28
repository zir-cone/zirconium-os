// src/fourty/block_device.cpp

// dude after finishing this if im ever drafted
// into war, and the enemy captures me,
// they aint getting shit outta my mouth
// cuz no torture methods they have are
// gonna be more painful than this FUCKING SHIT
// FUCK YOU COMPAQ AND WESTERN FUCKING DIGITAL!!!
// id actually rather make my own chips than
// do this bullshit again.
#include "block_device.h"
#include "../ports.h"
#include <stdint.h>

// We talk to the primary ATA bus, master drive, using LBA28 PIO.

static const uint16_t ATA_PRIMARY_IO     = 0x1F0;
static const uint16_t ATA_PRIMARY_CTRL   = 0x3F6;

// I/O port offsets from ATA_PRIMARY_IO
static const uint16_t ATA_REG_DATA       = 0x1F0; // 16-bit
static const uint16_t ATA_REG_ERROR      = 0x1F1;
static const uint16_t ATA_REG_FEATURES   = 0x1F1;
static const uint16_t ATA_REG_SECCOUNT0  = 0x1F2;
static const uint16_t ATA_REG_LBA0       = 0x1F3;
static const uint16_t ATA_REG_LBA1       = 0x1F4;
static const uint16_t ATA_REG_LBA2       = 0x1F5;
static const uint16_t ATA_REG_HDDEVSEL   = 0x1F6;
static const uint16_t ATA_REG_COMMAND    = 0x1F7;
static const uint16_t ATA_REG_STATUS     = 0x1F7;

// Control / alt status
static const uint16_t ATA_REG_ALTSTATUS  = 0x3F6;
static const uint16_t ATA_REG_DEVCTRL    = 0x3F6;

// Status bits
static const uint8_t ATA_SR_BSY  = 0x80;
static const uint8_t ATA_SR_DRDY = 0x40;
static const uint8_t ATA_SR_DF   = 0x20;
static const uint8_t ATA_SR_DSC  = 0x10;
static const uint8_t ATA_SR_DRQ  = 0x08;
static const uint8_t ATA_SR_CORR = 0x04;
static const uint8_t ATA_SR_IDX  = 0x02;
static const uint8_t ATA_SR_ERR  = 0x01;

// Commands
static const uint8_t ATA_CMD_READ_SECTORS  = 0x20;
static const uint8_t ATA_CMD_WRITE_SECTORS = 0x30;
static const uint8_t ATA_CMD_CACHE_FLUSH   = 0xE7;
static const uint8_t ATA_CMD_IDENTIFY      = 0xEC;

// Our disk config: 4 KiB blocks => 8 sectors per block
static const uint32_t SECTORS_PER_BLOCK = BD_BLOCK_SIZE / 512;

// Simple memcpy/memset (no libc)
static void* bd_memcpy(void* dst, const void* src, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < size; ++i) d[i] = s[i];
    return dst;
}

static void* bd_memset(void* dst, int value, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    for (uint32_t i = 0; i < size; ++i) d[i] = (uint8_t)value;
    return dst;
}

// --- ATA wait helpers ---

// Wait until BSY clears, or timeout (very crude)
static bool ata_wait_not_busy() {
    for (int i = 0; i < 100000; ++i) {
        uint8_t status = inb(ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) return true;
        io_wait();
    }
    return false;
}

// Wait for DRQ set and BSY clear
static bool ata_wait_drq() {
    for (int i = 0; i < 100000; ++i) {
        uint8_t status = inb(ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return false;
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) return true;
        io_wait();
    }
    return false;
}

// --- Sector read/write (one 512-byte sector via LBA28) ---

static bool ata_read_sector(uint32_t lba, void* buffer) {
    // Wait until not busy
    if (!ata_wait_not_busy()) return false;

    // Select drive: 0xE0 = 1110 0000b (LBA mode, master, high 4 bits of LBA)
    outb(ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    io_wait();

    // Set up sector count = 1
    outb(ATA_REG_SECCOUNT0, 1);

    // LBA low/mid/high
    outb(ATA_REG_LBA0,  (uint8_t)(lba & 0xFF));
    outb(ATA_REG_LBA1,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_REG_LBA2,  (uint8_t)((lba >> 16) & 0xFF));

    // Issue READ SECTORS command
    outb(ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

    if (!ata_wait_drq()) return false;

    // Read 256 words (512 bytes)
    uint16_t* buf = (uint16_t*)buffer;
    for (int i = 0; i < 256; ++i) {
        buf[i] = inw(ATA_REG_DATA);
    }

    return true;
}

// You just popped in to Kanye West "Get Right for the Summer" Workout Tape...

static bool ata_write_sector(uint32_t lba, const void* buffer) {
    if (!ata_wait_not_busy()) return false;

    outb(ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    io_wait();

    outb(ATA_REG_SECCOUNT0, 1);
    outb(ATA_REG_LBA0,  (uint8_t)(lba & 0xFF));
    outb(ATA_REG_LBA1,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_REG_LBA2,  (uint8_t)((lba >> 16) & 0xFF));

    outb(ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);

    if (!ata_wait_drq()) return false;

    const uint16_t* buf = (const uint16_t*)buffer;
    for (int i = 0; i < 256; ++i) {
        outw(ATA_REG_DATA, buf[i]);
    }

    // Flush cache
    outb(ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_not_busy();

    return true;
}

// --- IDENTIFY (detect primary master) ---

static bool ata_identify() {
    // Select master
    outb(ATA_REG_HDDEVSEL, 0xA0); // 1010 0000: master, CHS/LBA irrelevant yet
    io_wait();

    // Zero sector count and LBA regs per spec
    outb(ATA_REG_SECCOUNT0, 0);
    outb(ATA_REG_LBA0, 0);
    outb(ATA_REG_LBA1, 0);
    outb(ATA_REG_LBA2, 0);

    outb(ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    io_wait();

    uint8_t status = inb(ATA_REG_STATUS);
    if (status == 0) {
        // No device
        return false;
    }

    // Wait while BSY set
    while (status & ATA_SR_BSY) {
        status = inb(ATA_REG_STATUS);
    }

    // Check if it's ATA or ATAPI (if LBA1/LBA2 != 0, it's not ATA)
    uint8_t lba1 = inb(ATA_REG_LBA1);
    uint8_t lba2 = inb(ATA_REG_LBA2);
    if (lba1 != 0 || lba2 != 0) {
        // Not an ATA disk (likely ATAPI - CDROM), bail for now
        return false;
    }

    // Wait for DRQ
    while (!(status & ATA_SR_DRQ) && !(status & ATA_SR_ERR)) {
        status = inb(ATA_REG_STATUS);
    }

    if (status & ATA_SR_ERR) return false;

    // Read IDENTIFY data (256 words), but we ignore contents for now
    uint16_t scratch[256];
    for (int i = 0; i < 256; ++i) {
        scratch[i] = inw(ATA_REG_DATA);
    }

    return true;
}

// --- Public block API ---

bool bd_init() {
    // Reset controller: set SRST (bit 2) high then low (optional, simple for now)
    outb(ATA_REG_DEVCTRL, 0x04); // SRST=1, nIEN=0
    io_wait();
    outb(ATA_REG_DEVCTRL, 0x00);
    io_wait();

    // Try IDENTIFY primary master
    if (!ata_identify()) {
        // You can still return true if you want to ignore detection failures.
        return false;
    }

    return true;
}

// BD_BLOCK_SIZE is 4096, so we do 8 sectors per call
bool bd_read_block(uint32_t block_lba, void* buffer) {
    uint32_t first_sector = block_lba * SECTORS_PER_BLOCK;

    uint8_t temp[512];

    for (uint32_t i = 0; i < SECTORS_PER_BLOCK; ++i) {
        if (!ata_read_sector(first_sector + i, temp)) {
            return false;
        }
        bd_memcpy((uint8_t*)buffer + i * 512, temp, 512);
    }
    return true;
}

bool bd_write_block(uint32_t block_lba, const void* buffer) {
    uint32_t first_sector = block_lba * SECTORS_PER_BLOCK;

    uint8_t temp[512];

    for (uint32_t i = 0; i < SECTORS_PER_BLOCK; ++i) {
        bd_memcpy(temp, (const uint8_t*)buffer + i * 512, 512);
        if (!ata_write_sector(first_sector + i, temp)) {
            return false;
        }
    }
    return true;
}

// Our Father, which art in Heaven,
// hallowed be Thy name.
// Thy kingdom come.
// Thy will be done,
// on Earth as it is in Heaven.
// Give us this day our daily bread,
// and forgive us our trespasses 
// while we forgive those who trespass against us,
// and guide us not into temptation,
// but deliver us from evil.
// Amen.

// Hail Mary, full of grace,
// the Lord is with thee.
// Blessed art thou among women,
// and blessed is the fruit of thy womb, Jesus.
// Holy Mary, Mother of God, pray for us sinners,
// now and at the hour of our death.
// Amen.