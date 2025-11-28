// src/fourty/ffs.cpp
// main shit for the Fourty Filesystem
// make no mistake, girl i still love you
// Terry Davis really was God's chosen programmer

#include "ffs.h"
#include "block_device.h"
#include <stdint.h>

// internal helpers (no libc)

static void* k_memset(void* dst, int value, size_t size)
{
    uint8_t* p = (uint8_t*)dst;
    for (size_t i = 0; i < size; ++i) {
        p[i] = (uint8_t)value;
    }
    return dst;
}

static void* k_memcpy(void* dst, const void* src, size_t size)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < size; ++i) {
        d[i] = s[i];
    }
    return dst;
}

static size_t k_strlen(const char* s)
{
    size_t n = 0;
    while(s[n]) ++n;
    return n;
}

static int k_strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) break;
    }
    return 0;
}

static int k_strcmp(const char* a, const char* b)
{
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        ++i;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

static FFS_Superblock g_sb;
static bool g_mounted = false;

// fixed at 256 MiB for FFS 1.0
static const uint32_t FFS_BLOCK_SIZE        = 4096;
static const uint32_t FFS_TOTAL_BLOCKS      = 65536;
static const uint32_t FFS_BITMAP_START      = 1;
static const uint32_t FFS_BITMAP_BLOCKS     = 2;
static const uint32_t FFS_INODE_START       = 3;
static const uint32_t FFS_INODE_COUNT       = 8192;
static const uint32_t FFS_INODE_BLOCKS      = 512;
static const uint32_t FFS_DATA_START        = FFS_INODE_START;
static const uint32_t FFS_ROOT_INODE        = 1;

// internal block i/o wrappahs

static bool ffs_read_block(uint32_t block, void* buf)
{
    return bd_read_block(block, buf);
}

static bool ffs_write_block(uint32_t block, const void* buf) 
{
    return bd_write_block(block, buf);
}

// bitmap helpers
// 1 bit/block

static bool bitmap_get(uint32_t block)
{
    // block in [0, total_blocks)
    uint32_t rel = block;
    uint32_t byte_index = rel / 8;
    uint32_t bit_index  = rel % 8;
    uint8_t buffer[FFS_BLOCK_SIZE];
    uint32_t bitmap_block = FFS_BITMAP_BLOCKS + (byte_index / FFS_BLOCK_SIZE);
    uint32_t offset_in_block = byte_index % FFS_BLOCK_SIZE;

    if (!ffs_read_block(bitmap_block, buffer)) return true;
    uint8_t byte = buffer[offset_in_block];
    return (byte & (1 << bit_index)) != 0;
}


static bool bitmap_set(uint32_t block, bool used) {
    uint32_t rel = block;
    uint32_t byte_index = rel / 8;
    uint32_t bit_index  = rel % 8;

    uint8_t buffer[FFS_BLOCK_SIZE];
    uint32_t bitmap_block = FFS_BITMAP_START + (byte_index / FFS_BLOCK_SIZE);
    uint32_t offset_in_block = byte_index % FFS_BLOCK_SIZE;

    if (!ffs_read_block(bitmap_block, buffer)) return false;

    uint8_t byte = buffer[offset_in_block];
    if (used) {
        byte |= (1 << bit_index);
    } else {
        byte &= ~(1 << bit_index);
    }
    buffer[offset_in_block] = byte;

    return ffs_write_block(bitmap_block, buffer);
}

// Allocate a single block; return block index or 0 on failure.
// Note: block 0 is reserved for superblock, so we never allocate it.
static uint32_t alloc_block() {
    // For simplicity, linear scan. You can optimize later.
    for (uint32_t b = FFS_DATA_START; b < FFS_TOTAL_BLOCKS; ++b) {
        if (!bitmap_get(b)) {
            if (!bitmap_set(b, true)) return 0;
            return b;
        }
    }
    return 0; // no space
}

static void free_block(uint32_t block) {
    // Do not free reserved blocks
    if (block < FFS_DATA_START) return;
    bitmap_set(block, false);
}

// ---------- Inode helpers ----------

static bool read_inode(uint32_t inode_num, FFS_Inode* out) {
    if (inode_num == 0 || inode_num > FFS_INODE_COUNT) return false;
    uint32_t idx = inode_num - 1;
    uint32_t inodes_per_block = FFS_BLOCK_SIZE / sizeof(FFS_Inode); // 16
    uint32_t block_off = idx / inodes_per_block;
    uint32_t index_in_block = idx % inodes_per_block;

    uint8_t buffer[FFS_BLOCK_SIZE];
    if (!ffs_read_block(FFS_INODE_START + block_off, buffer)) return false;

    FFS_Inode* table = (FFS_Inode*)buffer;
    *out = table[index_in_block];
    return true;
}

static bool write_inode(uint32_t inode_num, const FFS_Inode* in) {
    if (inode_num == 0 || inode_num > FFS_INODE_COUNT) return false;
    uint32_t idx = inode_num - 1;
    uint32_t inodes_per_block = FFS_BLOCK_SIZE / sizeof(FFS_Inode);
    uint32_t block_off = idx / inodes_per_block;
    uint32_t index_in_block = idx % inodes_per_block;

    uint8_t buffer[FFS_BLOCK_SIZE];
    if (!ffs_read_block(FFS_INODE_START + block_off, buffer)) return false;

    FFS_Inode* table = (FFS_Inode*)buffer;
    table[index_in_block] = *in;

    return ffs_write_block(FFS_INODE_START + block_off, buffer);
}

// Allocate a free inode; returns inode number or 0 on failure.
static uint32_t alloc_inode(uint8_t type) {
    uint8_t buffer[FFS_BLOCK_SIZE];
    uint32_t inodes_per_block = FFS_BLOCK_SIZE / sizeof(FFS_Inode);

    for (uint32_t b = 0; b < FFS_INODE_BLOCKS; ++b) {
        if (!ffs_read_block(FFS_INODE_START + b, buffer)) return 0;
        FFS_Inode* table = (FFS_Inode*)buffer;
        for (uint32_t i = 0; i < inodes_per_block; ++i) {
            uint32_t inode_num = b * inodes_per_block + i + 1;
            if (inode_num == 0 || inode_num > FFS_INODE_COUNT) break;
            if (table[i].type == 0) {
                // Free inode
                k_memset(&table[i], 0, sizeof(FFS_Inode));
                table[i].type = type;
                table[i].size = 0;
                if (!ffs_write_block(FFS_INODE_START + b, buffer)) return 0;
                return inode_num;
            }
        }
    }
    return 0;
}

// ---------- Extent helpers ----------

// Find block that corresponds to byte offset within file, given inode.
// Returns block index and offset_in_block via out parameters.
// Returns false if offset >= size or no extent covers that offset.
static bool extent_locate_block(const FFS_Inode& inode,
                                uint64_t offset,
                                uint32_t& out_block,
                                uint32_t& out_offset_in_block) {
    uint64_t remaining = offset;
    for (int e = 0; e < 8; ++e) {
        const FFS_Extent& ex = inode.extents[e];
        if (ex.block_count == 0) continue;
        uint64_t extent_bytes = (uint64_t)ex.block_count * FFS_BLOCK_SIZE;
        if (remaining < extent_bytes) {
            uint32_t block_index_in_extent = (uint32_t)(remaining / FFS_BLOCK_SIZE);
            out_offset_in_block = (uint32_t)(remaining % FFS_BLOCK_SIZE);
            out_block = ex.start_block + block_index_in_extent;
            return true;
        }
        remaining -= extent_bytes;
    }
    return false;
}

// Ensure there is enough space to write 'size' bytes at 'offset'.
// Returns true on success and updates inode (but does NOT write it back to disk).
static bool ensure_file_capacity(FFS_Inode& inode, uint64_t offset, uint64_t size) {
    uint64_t end = offset + size;
    if (end <= inode.size) {
        return true; // already large enough
    }

    // How many bytes currently backed by extents?
    uint64_t backed = 0;
    for (int e = 0; e < 8; ++e) {
        const FFS_Extent& ex = inode.extents[e];
        if (ex.block_count == 0) continue;
        backed += (uint64_t)ex.block_count * FFS_BLOCK_SIZE;
    }

    if (end <= backed) {
        // Already enough extents, just update size
        inode.size = end;
        return true;
    }

    // Need more blocks
    uint64_t extra_bytes = end - backed;
    uint32_t extra_blocks = (uint32_t)((extra_bytes + FFS_BLOCK_SIZE - 1) / FFS_BLOCK_SIZE);

    // Try to extend last extent if possible
    int last_e = -1;
    for (int e = 7; e >= 0; --e) {
        if (inode.extents[e].block_count != 0) {
            last_e = e;
            break;
        }
    }

    while (extra_blocks > 0) {
        if (last_e >= 0) {
            // Try extending last extent by 1 block at a time
            uint32_t last_block = inode.extents[last_e].start_block +
                                  inode.extents[last_e].block_count - 1;
            uint32_t candidate = last_block + 1;
            if (candidate < FFS_TOTAL_BLOCKS && !bitmap_get(candidate)) {
                if (!bitmap_set(candidate, true)) return false;
                inode.extents[last_e].block_count += 1;
                extra_blocks -= 1;
                continue;
            }
        }

        // Need a new extent
        int new_e = -1;
        for (int e = 0; e < 8; ++e) {
            if (inode.extents[e].block_count == 0) {
                new_e = e;
                break;
            }
        }
        if (new_e < 0) {
            // No more extents available
            return false;
        }

        // Allocate at least 1 block, maybe more contiguous blocks
        uint32_t start = alloc_block();
        if (start == 0) return false;

        inode.extents[new_e].start_block = start;
        inode.extents[new_e].block_count = 1;
        extra_blocks -= 1;

        // Try to extend the new extent greedily for remaining blocks
        while (extra_blocks > 0) {
            uint32_t candidate = inode.extents[new_e].start_block +
                                 inode.extents[new_e].block_count;
            if (candidate < FFS_TOTAL_BLOCKS && !bitmap_get(candidate)) {
                if (!bitmap_set(candidate, true)) return false;
                inode.extents[new_e].block_count += 1;
                extra_blocks -= 1;
            } else {
                break;
            }
        }

        last_e = new_e;
    }

    inode.size = end;
    return true;
}

// ---------- Directory helpers ----------

static bool dir_add_entry(uint32_t dir_inode_num,
                          uint32_t child_inode_num,
                          uint8_t child_type,
                          const char* name);

static uint32_t dir_find_entry_inode(uint32_t dir_inode_num, const char* name);

// ---------- Path utilities ----------

static bool is_path_separator(char c) {
    return c == '/';
}

// Split path into parent + name.
// Example: "/foo/bar.txt" -> parent="/foo", name="bar.txt"
// Special: "/" → parent="/" name="" (used for root)
static bool split_parent_child(const char* path, char* parent_out, size_t parent_cap,
                               char* name_out, size_t name_cap) {
    size_t len = k_strlen(path);
    if (len == 0) return false;

    // If path == "/", parent = "/", name = ""
    if (len == 1 && path[0] == '/') {
        if (parent_cap > 0) {
            parent_out[0] = '/';
            if (parent_cap > 1) parent_out[1] = 0;
            else parent_out[0] = 0;
        }
        if (name_cap > 0) name_out[0] = 0;
        return true;
    }

    // Find last '/'
    int last_slash = -1;
    for (int i = (int)len - 1; i >= 0; --i) {
        if (path[i] == '/') {
            last_slash = i;
            break;
        }
    }
    if (last_slash < 0) return false; // invalid; for now require absolute

    size_t name_len = len - (size_t)last_slash - 1;
    if (name_len >= name_cap) return false;
    for (size_t i = 0; i < name_len; ++i) {
        name_out[i] = path[last_slash + 1 + i];
    }
    name_out[name_len] = 0;

    size_t parent_len = (size_t)last_slash;
    if (parent_len == 0) parent_len = 1; // root
    if (parent_len >= parent_cap) return false;

    for (size_t i = 0; i < parent_len; ++i) {
        parent_out[i] = path[i];
    }
    parent_out[parent_len] = 0;

    return true;
}

// ---------- Namespace ffs implementation ----------

namespace ffs 
{

    bool mount() {
        uint8_t buffer[FFS_BLOCK_SIZE];
        if (!ffs_read_block(0, buffer)) return false;
        FFS_Superblock* sb = (FFS_Superblock*)buffer;

        // Check magic
        if (sb->magic[0] != 'F' || sb->magic[1] != 'F' ||
            sb->magic[2] != 'S' || sb->magic[3] != '1') {
            return false;
        }

        // Basic sanity
        if (sb->block_size != FFS_BLOCK_SIZE) return false;
        if (sb->total_blocks != FFS_TOTAL_BLOCKS) return false;

        g_sb = *sb;
        g_mounted = true;
        return true;
    
    }
    uint64_t file_size(uint32_t inode) {
        FFS_Inode ino;
        if (!read_inode(inode, &ino)) {
            return 0;
        }
        return (uint64_t)ino.size;
    }

    bool init() {
        if (g_mounted) return true;
        if (ffs::mount()) return true;
        if (!format()) return false;
        return ffs::mount();
    }
    
    bool format() {
        // 1. Zero entire superblock buffer
        FFS_Superblock sb;
        k_memset(&sb, 0, sizeof(sb));
        sb.magic[0] = 'F';
        sb.magic[1] = 'F';
        sb.magic[2] = 'S';
        sb.magic[3] = '1';
        sb.version      = 1;
        sb.block_size   = FFS_BLOCK_SIZE;
        sb.total_blocks = FFS_TOTAL_BLOCKS;
    
        sb.bitmap_start      = FFS_BITMAP_START;
        sb.bitmap_blocks     = FFS_BITMAP_BLOCKS;
        sb.inode_table_start = FFS_INODE_START;
        sb.inode_count       = FFS_INODE_COUNT;
        sb.root_inode        = FFS_ROOT_INODE;
    
        // Write superblock
        uint8_t buffer[FFS_BLOCK_SIZE];
        k_memset(buffer, 0, sizeof(buffer));
        k_memcpy(buffer, &sb, sizeof(sb));
        if (!ffs_write_block(0, buffer)) return false;
    
        // 2. Zero bitmap blocks
        k_memset(buffer, 0, sizeof(buffer));
        for (uint32_t b = 0; b < FFS_BITMAP_BLOCKS; ++b) {
            if (!ffs_write_block(FFS_BITMAP_START + b, buffer)) return false;
        }
    
        // 3. Zero inode table blocks
        k_memset(buffer, 0, sizeof(buffer));
        for (uint32_t b = 0; b < FFS_INODE_BLOCKS; ++b) {
            if (!ffs_write_block(FFS_INODE_START + b, buffer)) return false;
        }
    
        // 4. Mark reserved blocks as used in bitmap
        for (uint32_t b = 0; b < FFS_DATA_START; ++b) {
            if (!bitmap_set(b, true)) return false;
        }
    
        // 5. Create root inode
        FFS_Inode root;
        k_memset(&root, 0, sizeof(root));
        root.type = 2; // dir
        root.size = 0;
        if (!write_inode(FFS_ROOT_INODE, &root)) return false;
    
        // 6. Save global superblock
        g_sb = sb;
        g_mounted = true;
    
        return true;
    }
    
    // bool init() {
    //     if (!bd_init()) return false;
    //     if (mount()) return true;
    //     if (!format()) return false;
    //     return mount();
    // }
    
    uint32_t root_inode() {
        return g_mounted ? g_sb.root_inode : 0;
    }
    
    // Look up a path starting from root, absolute paths only
    uint32_t lookup_path(const char* path) {
        if (!g_mounted) return 0;
        if (!path || path[0] == 0) return 0;
    
        // Root special case
        if (path[0] == '/' && path[1] == 0) {
            return g_sb.root_inode;
        }
    
        // Working buffer for component names
        char component[56];
        uint32_t current_inode = g_sb.root_inode;
    
        size_t i = 0;
        size_t len = k_strlen(path);
        // Skip leading '/'
        if (path[0] == '/') i = 1;
    
        while (i < len) {
            // Extract next component
            size_t pos = 0;
            while (i < len && !is_path_separator(path[i])) {
                if (pos < sizeof(component) - 1) {
                    component[pos++] = path[i];
                }
                ++i;
            }
            component[pos] = 0;
    
            if (pos == 0) {
                // Ignore sequences of '/'
                ++i;
                continue;
            }
    
            // Look up component in current directory
            uint32_t next_inode = dir_find_entry_inode(current_inode, component);
            if (next_inode == 0) return 0;
    
            current_inode = next_inode;
    
            // Skip '/'
            while (i < len && is_path_separator(path[i])) ++i;
        }
    
        return current_inode;
    }
    
    // Create empty file at path
    bool create_file(const char* path) {
        if (!g_mounted) return false;
        if (!path || path[0] != '/') return false;
    
        char parent[128];
        char name[56];
        if (!split_parent_child(path, parent, sizeof(parent), name, sizeof(name))) return false;
        if (name[0] == 0) return false;
    
        // Look up parent dir
        uint32_t parent_inode = lookup_path(parent);
        if (parent_inode == 0) return false;
    
        // Ensure no existing entry with same name
        if (dir_find_entry_inode(parent_inode, name) != 0) {
            return false; // already exists
        }
    
        uint32_t inode_num = alloc_inode(1); // file
        if (inode_num == 0) return false;
    
        // Add dir entry
        if (!dir_add_entry(parent_inode, inode_num, 1, name)) {
            // TODO: free inode and blocks (not allocating blocks yet for empty file)
            return false;
        }
    
        return true;
    }
    
    // Create directory
    bool create_dir(const char* path) {
        if (!g_mounted) return false;
        if (!path || path[0] != '/') return false;
    
        char parent[128];
        char name[56];
        if (!split_parent_child(path, parent, sizeof(parent), name, sizeof(name))) return false;
        if (name[0] == 0) return false;
    
        uint32_t parent_inode = lookup_path(parent);
        if (parent_inode == 0) return false;
    
        if (dir_find_entry_inode(parent_inode, name) != 0) {
            return false; // already exists
        }
    
        uint32_t inode_num = alloc_inode(2); // dir
        if (inode_num == 0) return false;
    
        // Add entry in parent
        if (!dir_add_entry(parent_inode, inode_num, 2, name)) {
            // TODO: free inode
            return false;
        }
    
        // Optionally, create '.' and '..' entries inside the new dir later.
    
        return true;
    }
    
    // Remove file/empty dir by path (very minimal, no recursive remove yet)
    bool remove_path(const char* path) {
        // TODO: implement (optional for v1)
        (void)path;
        return false;
    }
    
    int read_file(uint32_t inode_num, uint64_t offset, void* buffer, size_t size) {
        if (!g_mounted) return -1;
        if (inode_num == 0 || size == 0) return 0;
    
        FFS_Inode inode;
        if (!read_inode(inode_num, &inode)) return -1;
        if (inode.type != 1) return -1; // not file
    
        if (offset >= inode.size) return 0;
    
        if (offset + size > inode.size) {
            size = (size_t)(inode.size - offset);
        }
    
        uint8_t* out = (uint8_t*)buffer;
        size_t remaining = size;
        uint64_t off = offset;
    
        while (remaining > 0) {
            uint32_t block;
            uint32_t off_in_block;
            if (!extent_locate_block(inode, off, block, off_in_block)) break;
    
            uint8_t blkbuf[FFS_BLOCK_SIZE];
            if (!ffs_read_block(block, blkbuf)) return -1;
    
            size_t to_copy = FFS_BLOCK_SIZE - off_in_block;
            if (to_copy > remaining) to_copy = remaining;
    
            k_memcpy(out, blkbuf + off_in_block, to_copy);
    
            out += to_copy;
            off += to_copy;
            remaining -= to_copy;
        }
    
        return (int)(size - remaining);
    }
    
    int write_file(uint32_t inode_num, uint64_t offset, const void* buffer, size_t size) {
        if (!g_mounted) return -1;
        if (inode_num == 0 || size == 0) return 0;
    
        FFS_Inode inode;
        if (!read_inode(inode_num, &inode)) return -1;
        if (inode.type != 1) return -1; // not file
    
        // Ensure capacity
        if (!ensure_file_capacity(inode, offset, size)) return -1;
    
        const uint8_t* in = (const uint8_t*)buffer;
        size_t remaining = size;
        uint64_t off = offset;
    
        while (remaining > 0) {
            uint32_t block;
            uint32_t off_in_block;
            if (!extent_locate_block(inode, off, block, off_in_block)) break;
    
            uint8_t blkbuf[FFS_BLOCK_SIZE];
            // If writing full block, no need to read first
            if (off_in_block == 0 && remaining >= FFS_BLOCK_SIZE) {
                k_memcpy(blkbuf, in, FFS_BLOCK_SIZE);
            } else {
                if (!ffs_read_block(block, blkbuf)) return -1;
                size_t to_copy = FFS_BLOCK_SIZE - off_in_block;
                if (to_copy > remaining) to_copy = remaining;
                k_memcpy(blkbuf + off_in_block, in, to_copy);
            }
    
            if (!ffs_write_block(block, blkbuf)) return -1;
    
            size_t written_here = (remaining >= (FFS_BLOCK_SIZE - off_in_block))
                                    ? (FFS_BLOCK_SIZE - off_in_block)
                                    : remaining;
            in += written_here;
            off += written_here;
            remaining -= written_here;
        }
    
        // Update inode size if we wrote past previous end
        if (!write_inode(inode_num, &inode)) return -1;
    
        return (int)(size - remaining);
    }
    
    // Directory listing
    bool list_dir(uint32_t dir_inode_num,
                  void (*callback)(const FFS_DirEntry&)) {
        if (!g_mounted || !callback) return false;
    
        FFS_Inode inode;
        if (!read_inode(dir_inode_num, &inode)) return false;
        if (inode.type != 2) return false;
    
        uint64_t remaining = inode.size;
        uint64_t offset = 0;
    
        uint8_t blkbuf[FFS_BLOCK_SIZE];
    
        while (remaining > 0) {
            uint32_t block;
            uint32_t off_in_block;
            if (!extent_locate_block(inode, offset, block, off_in_block)) break;
    
            if (off_in_block != 0) {
                // For simplicity, assume directory entries are block-aligned
                // (we won't support partial block at start).
                return false;
            }
    
            if (!ffs_read_block(block, blkbuf)) return false;
    
            size_t entries_per_block = FFS_BLOCK_SIZE / sizeof(FFS_DirEntry);
            FFS_DirEntry* entries = (FFS_DirEntry*)blkbuf;
            for (size_t i = 0; i < entries_per_block; ++i) {
                if (entries[i].inode != 0) {
                    callback(entries[i]);
                }
            }
    
            offset += FFS_BLOCK_SIZE;
            if (remaining > FFS_BLOCK_SIZE) remaining -= FFS_BLOCK_SIZE;
            else remaining = 0;
        }
    
        return true;
    }
}// namespace ffs


// ---------- Directory helper implementations ----------

static bool dir_add_entry(uint32_t dir_inode_num,
                          uint32_t child_inode_num,
                          uint8_t child_type,
                          const char* name) {
    if (dir_inode_num == 0 || child_inode_num == 0) return false;

    FFS_Inode dir_inode;
    if (!read_inode(dir_inode_num, &dir_inode)) return false;
    if (dir_inode.type != 2) return false;

    size_t name_len = k_strlen(name);
    if (name_len == 0 || name_len > 55) return false;

    // Find a free DirEntry slot or extend directory by one block.
    uint64_t offset = 0;
    uint64_t dir_size = dir_inode.size;
    uint8_t blkbuf[FFS_BLOCK_SIZE];

    // If directory size is 0, allocate one block
    if (dir_size == 0) {
        if (!ensure_file_capacity(dir_inode, 0, FFS_BLOCK_SIZE)) return false;
        dir_size = dir_inode.size;
        if (!write_inode(dir_inode_num, &dir_inode)) return false;
    }

    while (true) {
        uint32_t block;
        uint32_t off_in_block;
        if (!extent_locate_block(dir_inode, offset, block, off_in_block)) {
            // No block covers this offset; extend dir by one block
            if (!ensure_file_capacity(dir_inode, dir_size, FFS_BLOCK_SIZE)) return false;
            dir_size = dir_inode.size;
            if (!write_inode(dir_inode_num, &dir_inode)) return false;
            // And loop again
            continue;
        }

        if (off_in_block != 0) {
            // Directory entries must be block-aligned
            return false;
        }

        if (!ffs_read_block(block, blkbuf)) return false;

        size_t entries_per_block = FFS_BLOCK_SIZE / sizeof(FFS_DirEntry);
        FFS_DirEntry* entries = (FFS_DirEntry*)blkbuf;

        for (size_t i = 0; i < entries_per_block; ++i) {
            if (entries[i].inode == 0) {
                // Free slot
                entries[i].inode    = child_inode_num;
                entries[i].type     = child_type;
                entries[i].name_len = (uint8_t)name_len;
                entries[i].reserved[0] = entries[i].reserved[1] = 0;
                k_memset(entries[i].name, 0, sizeof(entries[i].name));
                for (size_t j = 0; j < name_len && j < sizeof(entries[i].name) - 1; ++j) {
                    entries[i].name[j] = name[j];
                }

                if (!ffs_write_block(block, blkbuf)) return false;
                return true;
            }
        }

        offset += FFS_BLOCK_SIZE;
        if (offset >= dir_size) {
            // Need to extend dir; loop again
            if (!ensure_file_capacity(dir_inode, dir_size, FFS_BLOCK_SIZE)) return false;
            dir_size = dir_inode.size;
            if (!write_inode(dir_inode_num, &dir_inode)) return false;
        }
    }

    // Unreachable
    // return false;
}

static uint32_t dir_find_entry_inode(uint32_t dir_inode_num, const char* name) {
    if (dir_inode_num == 0) return 0;

    FFS_Inode dir_inode;
    if (!read_inode(dir_inode_num, &dir_inode)) return 0;
    if (dir_inode.type != 2) return 0;

    size_t name_len = k_strlen(name);
    if (name_len == 0 || name_len > 55) return 0;

    uint64_t remaining = dir_inode.size;
    uint64_t offset = 0;
    uint8_t blkbuf[FFS_BLOCK_SIZE];

    while (remaining > 0) {
        uint32_t block;
        uint32_t off_in_block;
        if (!extent_locate_block(dir_inode, offset, block, off_in_block)) break;

        if (off_in_block != 0) {
            // Directory entries must be block-aligned
            return 0;
        }

        if (!ffs_read_block(block, blkbuf)) return 0;

        size_t entries_per_block = FFS_BLOCK_SIZE / sizeof(FFS_DirEntry);
        FFS_DirEntry* entries = (FFS_DirEntry*)blkbuf;
        for (size_t i = 0; i < entries_per_block; ++i) {
            if (entries[i].inode == 0) continue;
            if (entries[i].name_len != name_len) continue;
            if (k_strncmp(entries[i].name, name, name_len) == 0) {
                return entries[i].inode;
            }
        }

        offset += FFS_BLOCK_SIZE;
        if (remaining > FFS_BLOCK_SIZE) remaining -= FFS_BLOCK_SIZE;
        else remaining = 0;
    }

    return 0;
}