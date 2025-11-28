// src/fourty/ffs.cpp
#include "ffs.h"
#include "block_device.h"
#include <stdint.h>
#include <stddef.h>

// Basic layout constants for a 256 MiB disk
#define FFS_BLOCK_SIZE       4096
#define FFS_TOTAL_BLOCKS     (256 * 1024 * 1024 / FFS_BLOCK_SIZE)  // 65536

// Layout:
// block 0               : superblock
// blocks [1..2]         : block bitmap (65536 bits = 8192 bytes = 2 blocks)
// blocks [3..(3+63)]    : inode table (1024 inodes * 256 bytes = 256 KiB = 64 blocks)
// blocks [67..end]      : data blocks
#define FFS_BITMAP_START           1
#define FFS_BITMAP_BLOCKS          2
#define FFS_INODE_TABLE_START      (FFS_BITMAP_START + FFS_BITMAP_BLOCKS)
#define FFS_INODE_COUNT            1024
#define FFS_INODES_PER_BLOCK       (FFS_BLOCK_SIZE / (int)sizeof(FFS_Inode))
#define FFS_INODE_TABLE_BLOCKS     ((FFS_INODE_COUNT + FFS_INODES_PER_BLOCK - 1) / FFS_INODES_PER_BLOCK)
#define FFS_DATA_START             (FFS_INODE_TABLE_START + FFS_INODE_TABLE_BLOCKS)

// Global state
static FFS_Superblock g_sb;
static bool           g_mounted = false;

// --- low-level block IO wrappers ---

static bool ffs_read_block(uint32_t lba, void* buffer) {
    return bd_read_block(lba, buffer);
}

static bool ffs_write_block(uint32_t lba, const void* buffer) {
    return bd_write_block(lba, buffer);
}

// --- bitmap helpers: 1 bit per block ---

static bool bitmap_get(uint32_t block, bool* used) {
    if (block >= FFS_TOTAL_BLOCKS) return false;
    uint32_t bit_index   = block;
    uint32_t byte_index  = bit_index >> 3;
    uint32_t blk_offset  = byte_index / FFS_BLOCK_SIZE;
    uint32_t byte_in_blk = byte_index % FFS_BLOCK_SIZE;

    if (blk_offset >= FFS_BITMAP_BLOCKS) return false;

    uint8_t buf[FFS_BLOCK_SIZE];
    if (!ffs_read_block(FFS_BITMAP_START + blk_offset, buf)) return false;

    uint8_t mask = (uint8_t)(1u << (bit_index & 7));
    *used = (buf[byte_in_blk] & mask) != 0;
    return true;
}

static bool bitmap_set(uint32_t block, bool used) {     // i hate this shit bro ive had to change this function like 20 times
    if (block >= FFS_TOTAL_BLOCKS) return false;
    uint32_t bit_index   = block;
    uint32_t byte_index  = bit_index >> 3;
    uint32_t blk_offset  = byte_index / FFS_BLOCK_SIZE;
    uint32_t byte_in_blk = byte_index % FFS_BLOCK_SIZE;

    if (blk_offset >= FFS_BITMAP_BLOCKS) return false;

    uint8_t buf[FFS_BLOCK_SIZE];
    if (!ffs_read_block(FFS_BITMAP_START + blk_offset, buf)) return false;

    uint8_t mask = (uint8_t)(1u << (bit_index & 7));
    if (used)
        buf[byte_in_blk] |= mask;
    else
        buf[byte_in_blk] &= (uint8_t)~mask;

    if (!ffs_write_block(FFS_BITMAP_START + blk_offset, buf)) return false;
    return true;
}

static int alloc_block() {
    // Only allocate from data region
    for (uint32_t b = FFS_DATA_START; b < FFS_TOTAL_BLOCKS; ++b) {
        bool used = true;
        if (!bitmap_get(b, &used)) return -1;
        if (!used) {
            if (!bitmap_set(b, true)) return -1;
            // zero the block
            uint8_t z[FFS_BLOCK_SIZE];
            for (uint32_t i = 0; i < FFS_BLOCK_SIZE; ++i) z[i] = 0;
            if (!ffs_write_block(b, z)) return -1;
            return (int)b;
        }
    }
    return -1;
}

static void free_block(uint32_t b) {
    if (b < FFS_DATA_START || b >= FFS_TOTAL_BLOCKS) return;
    bitmap_set(b, false);
} // IM GONNA KILL MYSELF

// --- inode helpers ---

static bool read_inode(uint32_t inode_num, FFS_Inode* out) {
    if (inode_num == 0 || inode_num > g_sb.inode_count) return false;
    uint32_t idx        = inode_num - 1;
    uint32_t blk_index  = idx / FFS_INODES_PER_BLOCK;
    uint32_t ino_index  = idx % FFS_INODES_PER_BLOCK;
    uint8_t  buf[FFS_BLOCK_SIZE];

    if (!ffs_read_block(g_sb.inode_table_start + blk_index, buf)) return false;

    FFS_Inode* arr = (FFS_Inode*)buf;
    *out = arr[ino_index];
    return true;
}

static bool write_inode(uint32_t inode_num, const FFS_Inode* in) {
    if (inode_num == 0 || inode_num > g_sb.inode_count) return false;
    uint32_t idx        = inode_num - 1;
    uint32_t blk_index  = idx / FFS_INODES_PER_BLOCK;
    uint32_t ino_index  = idx % FFS_INODES_PER_BLOCK;
    uint8_t  buf[FFS_BLOCK_SIZE];

    if (!ffs_read_block(g_sb.inode_table_start + blk_index, buf)) return false;
    // about to just kill myself
    FFS_Inode* arr = (FFS_Inode*)buf;
    arr[ino_index] = *in;

    if (!ffs_write_block(g_sb.inode_table_start + blk_index, buf)) return false;
    return true;
}

static uint32_t alloc_inode() {
    for (uint32_t i = 1; i <= g_sb.inode_count; ++i) {
        FFS_Inode ino;
        if (!read_inode(i, &ino)) return 0;
        if (ino.type == 0) {
            // clear it
            ino.type      = 0;
            ino.flags     = 0;
            ino.reserved0 = 0;
            ino.size      = 0;
            for (int j = 0; j < 8; ++j) {
                ino.extents[j].start_block = 0;
                ino.extents[j].block_count = 0;
            }
            for (size_t j = 0; j < sizeof(ino.reserved); ++j) {
                ino.reserved[j] = 0;
            }
            if (!write_inode(i, &ino)) return 0;
            return i;
        }   
    }
    return 0;
}

// --- directory helpers ---
// We only support a single extent for directories (one data block).

#define FFS_DIRENTRIES_PER_BLOCK (FFS_BLOCK_SIZE / (int)sizeof(FFS_DirEntry))

static bool dir_load_block(uint32_t dir_inode, FFS_Inode* dir_ino, uint8_t* buf) {
    if (!read_inode(dir_inode, dir_ino)) return false;
    if (dir_ino->type != 2) return false; // not a directory
    if (dir_ino->extents[0].start_block == 0 || dir_ino->extents[0].block_count == 0) {
        return false;
    }
    uint32_t block = dir_ino->extents[0].start_block;
    if (!ffs_read_block(block, buf)) return false;
    return true; // im actually just gonna kill myself this shit is infuriating
}

static bool dir_save_block(uint32_t dir_inode, const FFS_Inode* dir_ino, const uint8_t* buf) {
    if (dir_ino->extents[0].start_block == 0 || dir_ino->extents[0].block_count == 0) {
        return false;
    }
    uint32_t block = dir_ino->extents[0].start_block;
    if (!ffs_write_block(block, buf)) return false;
    if (!write_inode(dir_inode, dir_ino)) return false;
    return true;
}

static bool dir_find_entry(uint32_t dir_inode, const char* name, size_t name_len, FFS_DirEntry* out) {
    FFS_Inode dir_ino;
    uint8_t   buf[FFS_BLOCK_SIZE];
    if (!dir_load_block(dir_inode, &dir_ino, buf)) return false;

    FFS_DirEntry* ents = (FFS_DirEntry*)buf;
    for (int i = 0; i < FFS_DIRENTRIES_PER_BLOCK; ++i) {
        if (ents[i].inode == 0 || ents[i].name_len == 0) continue;
        if (ents[i].name_len != name_len) continue;
        bool match = true;
        for (size_t j = 0; j < name_len; ++j) {
            if (ents[i].name[j] != name[j]) {
                match = false; break;
                // FUCK
            }
        }
        if (match) {
            if (out) *out = ents[i];
            return true; 
        }
    }
    return false;
}

static bool dir_add_entry(uint32_t dir_inode, uint32_t inode_num, uint8_t type,
                          const char* name, size_t name_len) {
    if (name_len > 55) name_len = 55;

    FFS_Inode dir_ino;
    uint8_t   buf[FFS_BLOCK_SIZE];

    if (!read_inode(dir_inode, &dir_ino)) return false;
    if (dir_ino.type != 2) return false;

    if (dir_ino.extents[0].start_block == 0 || dir_ino.extents[0].block_count == 0) {
        // allocate data block for this dir
        int b = alloc_block();
        if (b < 0) return false;
        dir_ino.extents[0].start_block = (uint32_t)b;
        dir_ino.extents[0].block_count = 1;
        // zero it
        for (uint32_t i = 0; i < FFS_BLOCK_SIZE; ++i) buf[i] = 0;
    } else {
        if (!ffs_read_block(dir_ino.extents[0].start_block, buf)) return false;
    }

    FFS_DirEntry* ents = (FFS_DirEntry*)buf;

    // find free slot
    for (int i = 0; i < FFS_DIRENTRIES_PER_BLOCK; ++i) {
        if (ents[i].inode == 0 || ents[i].name_len == 0) {
            ents[i].inode    = inode_num;
            ents[i].type     = type;
            ents[i].name_len = (uint8_t)name_len;
            ents[i].reserved[0] = 0;
            ents[i].reserved[1] = 0;
            for (size_t j = 0; j < 56; ++j) ents[i].name[j] = 0;
            for (size_t j = 0; j < name_len && j < 56; ++j) {
                ents[i].name[j] = name[j];
            }
            // update dir size (simple: count entries)
            dir_ino.size = (uint64_t)FFS_BLOCK_SIZE;
            if (!dir_save_block(dir_inode, &dir_ino, buf)) return false;
            return true;
        }
    }

    // directory full (only 1 block supported)
    return false;
}

static bool dir_remove_entry(uint32_t dir_inode, const char* name, size_t name_len) {
    FFS_Inode dir_ino;
    uint8_t   buf[FFS_BLOCK_SIZE];
    if (!dir_load_block(dir_inode, &dir_ino, buf)) return false;

    FFS_DirEntry* ents = (FFS_DirEntry*)buf;    // FUCKING RED SQUIGGLY LINES 
    for (int i = 0; i < FFS_DIRENTRIES_PER_BLOCK; ++i) {    // piece of SHIT
        if (ents[i].inode == 0 || ents[i].name_len == 0) continue;
        if (ents[i].name_len != name_len) continue;
        bool match = true;
        for (size_t j = 0; j < name_len; ++j) {
            if (ents[i].name[j] != name[j]) { match = false; break; }
        }
        if (match) {
            ents[i].inode    = 0;
            ents[i].type     = 0;
            ents[i].name_len = 0;
            for (size_t j = 0; j < 56; ++j) ents[i].name[j] = 0;
            if (!dir_save_block(dir_inode, &dir_ino, buf)) return false;
            return true;
        } // IM GOING TO KILl MYSELF
    }
    return false;
}

static bool dir_is_empty_except_dots(uint32_t dir_inode) {
    FFS_Inode dir_ino;
    uint8_t   buf[FFS_BLOCK_SIZE];
    if (!dir_load_block(dir_inode, &dir_ino, buf)) return false;

    FFS_DirEntry* ents = (FFS_DirEntry*)buf;
    for (int i = 0; i < FFS_DIRENTRIES_PER_BLOCK; ++i) {
        if (ents[i].inode == 0 || ents[i].name_len == 0) continue;
        // '.' or '..'?
        if (ents[i].name_len == 1 && ents[i].name[0] == '.') continue;
        if (ents[i].name_len == 2 && ents[i].name[0] == '.' && ents[i].name[1] == '.') continue;
        return false;
    }
    return true;
}

// Initialize a directory inode's data block with '.' and '..'
static bool dir_init_dot_entries(uint32_t dir_inode, uint32_t parent_inode) {
    FFS_Inode dir_ino;
    uint8_t   buf[FFS_BLOCK_SIZE];

    if (!read_inode(dir_inode, &dir_ino)) return false;
    if (dir_ino.extents[0].start_block == 0 || dir_ino.extents[0].block_count == 0) {
        int b = alloc_block();
        if (b < 0) return false;
        dir_ino.extents[0].start_block = (uint32_t)b;
        dir_ino.extents[0].block_count = 1;
    }

    for (uint32_t i = 0; i < FFS_BLOCK_SIZE; ++i) buf[i] = 0;

    FFS_DirEntry* ents = (FFS_DirEntry*)buf;

    // '.'
    ents[0].inode    = dir_inode;
    ents[0].type     = 2;
    ents[0].name_len = 1;
    ents[0].reserved[0] = ents[0].reserved[1] = 0;
    ents[0].name[0]  = '.';
    for (int i = 1; i < 56; ++i) ents[0].name[i] = 0;

    // '..'
    ents[1].inode    = parent_inode;
    ents[1].type     = 2;
    ents[1].name_len = 2;
    ents[1].reserved[0] = ents[1].reserved[1] = 0;
    ents[1].name[0]  = '.';
    ents[1].name[1]  = '.';
    for (int i = 2; i < 56; ++i) ents[1].name[i] = 0;

    dir_ino.type = 2;
    dir_ino.flags = 0;
    dir_ino.size  = (uint64_t)FFS_BLOCK_SIZE;

    if (!dir_save_block(dir_inode, &dir_ino, buf)) return false;
    return true;
}

// --- path walking helpers ---

static size_t k_strlen(const char* s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

// Walk absolute path like "/foo/bar". Returns inode or 0.
static uint32_t walk_path(const char* path) {
    if (!g_mounted) return 0;
    if (!path || path[0] == 0) return 0;
    if (path[0] != '/') return 0;

    uint32_t inode = g_sb.root_inode;
    const char* p = path;

    // skip leading '/'
    while (*p == '/') ++p;

    char name[56];

    while (*p) {
        // extract component
        size_t len = 0;
        while (*p && *p != '/' && len + 1 < sizeof(name)) {
            name[len++] = *p++;
        }
        name[len] = 0;

        // skip duplicate '/'
        while (*p == '/') ++p;

        if (len == 0) continue;

        // '.' and '..'
        if (len == 1 && name[0] == '.') {
            continue;
        }
        if (len == 2 && name[0] == '.' && name[1] == '.') {
            // go to parent dir: find ".." entry
            FFS_DirEntry ent;
            if (!dir_find_entry(inode, "..", 2, &ent)) {
                // if something weird, stay
                continue;
            }
            inode = ent.inode;
            continue;
        }

        // normal component
        FFS_DirEntry ent;
        if (!dir_find_entry(inode, name, len, &ent)) {
            return 0;
        }
        inode = ent.inode;
    }

    return inode;
}

// Split "/foo/bar.txt" => parent "/foo", name "bar.txt"
static bool split_parent_child(const char* path,
                               char* parent, size_t parent_cap,
                               char* name, size_t name_cap) {
    if (!path || path[0] != '/') return false;
    size_t len = k_strlen(path);
    if (len < 2) return false;

    // find last '/'
    size_t last_slash = 0;
    for (size_t i = 0; i < len; ++i) {
        if (path[i] == '/') last_slash = i;
    }

    if (last_slash == len - 1) {
        // trailing slash: strip it and find again
        // also fuck me in the ass im gonna kill myself
        len--;
        if (len < 2) return false;
        last_slash = 0;
        for (size_t i = 0; i < len; ++i) {
            if (path[i] == '/') last_slash = i;
        }
    }

    size_t parent_len = (last_slash == 0) ? 1 : last_slash;
    size_t name_len   = len - last_slash - 1;

    if (parent_len + 1 > parent_cap) return false;
    if (name_len + 1 > name_cap) return false;

    // parent
    if (last_slash == 0) {
        parent[0] = '/';
        parent[1] = 0;
    } else {
        for (size_t i = 0; i < parent_len; ++i) parent[i] = path[i];
        parent[parent_len] = 0;
    }

    // name
    for (size_t i = 0; i < name_len; ++i) name[i] = path[last_slash + 1 + i];
    name[name_len] = 0;
    return true;
}

// --------------------------------------------------------
// Public FFS API
// --------------------------------------------------------

namespace ffs {

bool mount() {
    uint8_t buffer[FFS_BLOCK_SIZE];
    if (!ffs_read_block(0, buffer)) return false;
    FFS_Superblock* sb = (FFS_Superblock*)buffer;

    if (sb->magic[0] != 'F' || sb->magic[1] != 'F' ||
        sb->magic[2] != 'S' || sb->magic[3] != '0') {
        return false;
    }

    if (sb->block_size != FFS_BLOCK_SIZE) return false;
    if (sb->total_blocks != FFS_TOTAL_BLOCKS) return false;

    g_sb       = *sb;
    g_mounted  = true;
    return true;
}

bool format() {
    // Build superblock
    for (size_t i = 0; i < sizeof(g_sb); ++i) ((uint8_t*)&g_sb)[i] = 0;

    g_sb.magic[0]        = 'F';
    g_sb.magic[1]        = 'F';
    g_sb.magic[2]        = '4';
    g_sb.magic[3]        = '0';
    g_sb.version         = 1;
    g_sb.block_size      = FFS_BLOCK_SIZE;
    g_sb.total_blocks    = FFS_TOTAL_BLOCKS;
    g_sb.bitmap_start    = FFS_BITMAP_START;
    g_sb.bitmap_blocks   = FFS_BITMAP_BLOCKS;
    g_sb.inode_table_start = FFS_INODE_TABLE_START;
    g_sb.inode_count     = FFS_INODE_COUNT;
    g_sb.root_inode      = 1;

    // write superblock
    if (!ffs_write_block(0, &g_sb)) return false;

    // clear bitmap and inode table blocks
    uint8_t zero[FFS_BLOCK_SIZE];
    for (uint32_t i = 0; i < FFS_BLOCK_SIZE; ++i) zero[i] = 0;

    for (uint32_t b = FFS_BITMAP_START;
         b < FFS_BITMAP_START + FFS_BITMAP_BLOCKS + FFS_INODE_TABLE_BLOCKS;
         ++b) {
        if (!ffs_write_block(b, zero)) return false;
    }

    // mark metadata blocks used in bitmap
    for (uint32_t b = 0; b < FFS_DATA_START; ++b) {
        bitmap_set(b, true);
    }

    // initialize root inode (1) as directory
    FFS_Inode root;
    root.type      = 2; // dir
    root.flags     = 0;
    root.reserved0 = 0;
    root.size      = 0;
    for (int i = 0; i < 8; ++i) {
        root.extents[i].start_block = 0;
        root.extents[i].block_count = 0;
    }
    for (size_t i = 0; i < sizeof(root.reserved); ++i) root.reserved[i] = 0;

    if (!write_inode(1, &root)) return false;
    if (!dir_init_dot_entries(1, 1)) return false;

    g_mounted = true;
    return true;
}

bool init() {
    if (g_mounted) return true;
    if (mount()) return true;
    if (!format()) return false;
    return mount();
}

uint32_t root_inode() {
    return g_sb.root_inode;
}

uint64_t file_size(uint32_t inode) {    // dont change this shit again itll break ALL your shit, dumbass
    FFS_Inode ino;
    if (!read_inode(inode, &ino)) return 0;
    return ino.size;
}

uint32_t lookup_path(const char* path) {
    return walk_path(path);
}

int read_file(uint32_t inode_num, uint64_t offset, void* buffer, uint32_t length) {
    if (!g_mounted) return -1;
    if (length == 0) return 0;

    FFS_Inode ino;
    if (!read_inode(inode_num, &ino)) return -1;
    if (ino.type != 1) return -1; // not a file

    if (offset >= ino.size) return 0;

    if (offset + length > ino.size) {
        length = (uint32_t)(ino.size - offset);
    }

    if (ino.extents[0].start_block == 0 || ino.extents[0].block_count == 0) {
        return 0;
    }

    if (offset >= FFS_BLOCK_SIZE) {
        // we only support single-block small files
        return 0;
    }

    uint8_t buf[FFS_BLOCK_SIZE];
    if (!ffs_read_block(ino.extents[0].start_block, buf)) return -1;

    if (offset + length > FFS_BLOCK_SIZE) {
        length = FFS_BLOCK_SIZE - (uint32_t)offset;
    }

    for (uint32_t i = 0; i < length; ++i) {
        ((uint8_t*)buffer)[i] = buf[(uint32_t)offset + i];
    }

    return (int)length;
}

int write_file(uint32_t inode_num, uint64_t offset, const void* buffer, uint32_t length) {
    if (!g_mounted) return -1;
    if (length == 0) return 0;

    if (offset >= FFS_BLOCK_SIZE) {
        // no multi-block support
        return -1;
    }

    if (offset + length > FFS_BLOCK_SIZE) {
        length = FFS_BLOCK_SIZE - (uint32_t)offset;
    }

    FFS_Inode ino;
    if (!read_inode(inode_num, &ino)) return -1;
    if (ino.type != 1) return -1; // not a file

    if (ino.extents[0].start_block == 0 || ino.extents[0].block_count == 0) {
        int b = alloc_block();
        if (b < 0) return -1;
        ino.extents[0].start_block = (uint32_t)b;
        ino.extents[0].block_count = 1;
        ino.size = 0;
    }

    uint8_t buf[FFS_BLOCK_SIZE];
    if (!ffs_read_block(ino.extents[0].start_block, buf)) return -1;

    for (uint32_t i = 0; i < length; ++i) {
        buf[(uint32_t)offset + i] = ((const uint8_t*)buffer)[i];
    }

    if (!ffs_write_block(ino.extents[0].start_block, buf)) return -1;

    uint64_t end = offset + length;
    if (end > ino.size) ino.size = end;
    if (!write_inode(inode_num, &ino)) return -1;

    return (int)length;
}

bool list_dir(uint32_t inode_num, void (*callback)(const FFS_DirEntry&)) {
    if (!g_mounted) return false;
    if (!callback) return false;

    FFS_Inode dir_ino;
    uint8_t   buf[FFS_BLOCK_SIZE];
    if (!dir_load_block(inode_num, &dir_ino, buf)) return false;

    FFS_DirEntry* ents = (FFS_DirEntry*)buf;
    for (int i = 0; i < FFS_DIRENTRIES_PER_BLOCK; ++i) {
        if (ents[i].inode == 0 || ents[i].name_len == 0) continue;
        callback(ents[i]);
        // fuck im gonna kill myself
    }
    return true;
}

bool create_dir(const char* path) {
    if (!g_mounted) return false;
    if (!path || path[0] != '/') return false;

    char parent[256];
    char name[56];
    if (!split_parent_child(path, parent, sizeof(parent), name, sizeof(name))) return false;

    uint32_t parent_inode = walk_path(parent);
    if (parent_inode == 0) return false;

    // already exists?
    FFS_DirEntry dummy;
    if (dir_find_entry(parent_inode, name, k_strlen(name), &dummy)) {
        return false;
    }

    uint32_t inode_num = alloc_inode();
    if (inode_num == 0) return false;

    FFS_Inode ino;
    if (!read_inode(inode_num, &ino)) return false;
    ino.type  = 2;
    ino.flags = 0;
    ino.size  = 0;
    if (!write_inode(inode_num, &ino)) return false;

    if (!dir_init_dot_entries(inode_num, parent_inode)) return false;

    if (!dir_add_entry(parent_inode, inode_num, 2, name, k_strlen(name))) return false;

    return true;
}

bool create_file(const char* path) {
    if (!g_mounted) return false;
    if (!path || path[0] != '/') return false;

    char parent[256];
    char name[56];
    if (!split_parent_child(path, parent, sizeof(parent), name, sizeof(name))) return false;

    uint32_t parent_inode = walk_path(parent);
    if (parent_inode == 0) return false;

    // already exists?
    FFS_DirEntry dummy;
    if (dir_find_entry(parent_inode, name, k_strlen(name), &dummy)) {
        return false;
    }

    uint32_t inode_num = alloc_inode();
    if (inode_num == 0) return false;

    FFS_Inode ino;
    if (!read_inode(inode_num, &ino)) return false;
    ino.type  = 1; // file
    ino.flags = 0;
    ino.size  = 0;
    // no data blocks yet
    if (!write_inode(inode_num, &ino)) return false;

    if (!dir_add_entry(parent_inode, inode_num, 1, name, k_strlen(name))) return false;
    return true;
}

bool remove_path(const char* path) {
    if (!g_mounted) return false;
    if (!path || path[0] != '/') return false;

    char parent[256];
    char name[56];
    if (!split_parent_child(path, parent, sizeof(parent), name, sizeof(name))) return false;

    uint32_t parent_inode = walk_path(parent);
    if (parent_inode == 0) return false;

    uint32_t target_inode = walk_path(path);
    if (target_inode == 0) return false;

    FFS_Inode ino;
    if (!read_inode(target_inode, &ino)) return false;

    if (ino.type == 2) {
        // directory: must be empty except . and ..
        if (!dir_is_empty_except_dots(target_inode)) return false;
    }

    // free file data blocks (only first extent)
    if (ino.type == 1 && ino.extents[0].start_block != 0) {
        free_block(ino.extents[0].start_block);
        ino.extents[0].start_block = 0;
        ino.extents[0].block_count = 0;
    }

    // mark inode free
    ino.type = 0;
    ino.size = 0;
    if (!write_inode(target_inode, &ino)) return false;

    // remove from parent dir
    if (!dir_remove_entry(parent_inode, name, k_strlen(name))) return false;

    return true;
}

} // namespace ffs