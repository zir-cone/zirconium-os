#include "ffs.h"
#include "block_device.h"
#include "ffs_core.h"
#include <stdint.h>
#include <stddef.h>

static const uint64_t FFS_BLOCK_SIZE = 4096;
static const uint64_t FFS_TOTAL_BLOCKS = (256 * 1024 * 1024 / FFS_BLOCK_SIZE);

static const uint64_t FFS_FAT_START = 1;
static const uint64_t FFS_FAT_BYTES = FFS_TOTAL_BLOCKS * sizeof(uint64_t);
static const uint64_t FFS_FAT_BLOCKS = (FFS_FAT_BYTES + FFS_BLOCK_SIZE - 1) / FFS_BLOCK_SIZE;
static const uint64_t FFS_INODE_COUNT = 1024;
static const uint64_t FFS_INODES_PER_BLOCK = FFS_BLOCK_SIZE / (uint64_t)sizeof(FFS_Inode);
static const uint64_t FFS_INODE_TABLE_BLOCKS =
    (FFS_INODE_COUNT + FFS_INODES_PER_BLOCK - 1) / FFS_INODES_PER_BLOCK;
static const uint64_t FFS_INODE_TABLE_START = FFS_FAT_START + FFS_FAT_BLOCKS;
static const uint64_t FFS_DATA_START = FFS_INODE_TABLE_START + FFS_INODE_TABLE_BLOCKS;

static const uint64_t FFS_FAT_FREE = 0;
static const uint64_t FFS_FAT_EOF = 0xFFFFFFFFFFFFFFFFULL;

static FFS_Superblock g_sb;
static bool g_mounted = false;

static bool ffs_read_block(uint64_t lba, void* buffer) {
    if (lba > 0xFFFFFFFFULL) return false;
    return bd_read_block((uint32_t)lba, buffer);
}

static bool ffs_write_block(uint64_t lba, const void* buffer) {
    if (lba > 0xFFFFFFFFULL) return false;
    return bd_write_block((uint32_t)lba, buffer);
}

static bool fat_read_entry(uint64_t cluster, uint64_t* out) {
    if (!out || cluster >= g_sb.total_blocks) return false;
    uint64_t byte_index = cluster * sizeof(uint64_t);
    uint64_t block_index = byte_index / FFS_BLOCK_SIZE;
    uint64_t offset = byte_index % FFS_BLOCK_SIZE;

    if (block_index >= g_sb.fat_blocks) return false;

    uint8_t buf[FFS_BLOCK_SIZE];
    if (!ffs_read_block(g_sb.fat_start + block_index, buf)) return false;

    uint64_t value = 0;
    const uint8_t* p = buf + offset;
    for (uint32_t i = 0; i < sizeof(uint64_t); ++i) {
        value |= ((uint64_t)p[i]) << (i * 8);
    }
    *out = value;
    return true;
}

static bool fat_write_entry(uint64_t cluster, uint64_t value) {
    if (cluster >= g_sb.total_blocks) return false;
    uint64_t byte_index = cluster * sizeof(uint64_t);
    uint64_t block_index = byte_index / FFS_BLOCK_SIZE;
    uint64_t offset = byte_index % FFS_BLOCK_SIZE;

    if (block_index >= g_sb.fat_blocks) return false;

    uint8_t buf[FFS_BLOCK_SIZE];
    if (!ffs_read_block(g_sb.fat_start + block_index, buf)) return false;

    uint8_t* p = buf + offset;
    for (uint32_t i = 0; i < sizeof(uint64_t); ++i) {
        p[i] = (uint8_t)((value >> (i * 8)) & 0xFF);
    }

    if (!ffs_write_block(g_sb.fat_start + block_index, buf)) return false;
    return true;
}

static uint64_t alloc_cluster() {
    for (uint64_t b = FFS_DATA_START; b < g_sb.total_blocks; ++b) {
        uint64_t entry = FFS_FAT_EOF;
        if (!fat_read_entry(b, &entry)) return 0;
        if (entry == FFS_FAT_FREE) {
            if (!fat_write_entry(b, FFS_FAT_EOF)) return 0;
            uint8_t z[FFS_BLOCK_SIZE];
            ffs_memzero(z, FFS_BLOCK_SIZE);
            if (!ffs_write_block(b, z)) return 0;
            return b;
        }
    }
    return 0;
}

static void free_cluster_chain(uint64_t start_cluster) {
    uint64_t current = start_cluster;
    while (current != 0 && current != FFS_FAT_EOF) {
        uint64_t next = FFS_FAT_EOF;
        if (!fat_read_entry(current, &next)) return;
        fat_write_entry(current, FFS_FAT_FREE);
        current = next;
    }
}

static bool read_inode(uint32_t inode_num, FFS_Inode* out) {
    if (!out || inode_num == 0 || inode_num > g_sb.inode_count) return false;
    uint32_t idx = inode_num - 1;
    uint32_t blk_index = idx / (uint32_t)FFS_INODES_PER_BLOCK;
    uint32_t ino_index = idx % (uint32_t)FFS_INODES_PER_BLOCK;
    uint8_t buf[FFS_BLOCK_SIZE];

    if (!ffs_read_block(g_sb.inode_table_start + blk_index, buf)) return false;

    const FFS_Inode* arr = (const FFS_Inode*)buf;
    *out = arr[ino_index];
    return true;
}

static bool write_inode(uint32_t inode_num, const FFS_Inode* in) {
    if (!in || inode_num == 0 || inode_num > g_sb.inode_count) return false;
    uint32_t idx = inode_num - 1;
    uint32_t blk_index = idx / (uint32_t)FFS_INODES_PER_BLOCK;
    uint32_t ino_index = idx % (uint32_t)FFS_INODES_PER_BLOCK;
    uint8_t buf[FFS_BLOCK_SIZE];

    if (!ffs_read_block(g_sb.inode_table_start + blk_index, buf)) return false;
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
            ffs_memzero(&ino, sizeof(ino));
            if (!write_inode(i, &ino)) return 0;
            return i;
        }
    }
    return 0;
}

#define FFS_DIRENTRIES_PER_BLOCK (FFS_BLOCK_SIZE / (int)sizeof(FFS_DirEntry))

static bool dir_load_block(uint32_t dir_inode, FFS_Inode* dir_ino, uint8_t* buf) {
    if (!read_inode(dir_inode, dir_ino)) return false;
    if (dir_ino->type != 2) return false;
    if (dir_ino->first_cluster == 0 || dir_ino->first_cluster == FFS_FAT_EOF) {
        return false;
    }
    if (!ffs_read_block(dir_ino->first_cluster, buf)) return false;
    return true;
}

static bool dir_save_block(uint32_t dir_inode, const FFS_Inode* dir_ino, const uint8_t* buf) {
    if (dir_ino->first_cluster == 0 || dir_ino->first_cluster == FFS_FAT_EOF) {
        return false;
    }
    if (!ffs_write_block(dir_ino->first_cluster, buf)) return false;
    if (!write_inode(dir_inode, dir_ino)) return false;
    return true;
}

static bool dir_find_entry(uint32_t dir_inode, const char* name, size_t name_len, FFS_DirEntry* out) {
    FFS_Inode dir_ino;
    uint8_t buf[FFS_BLOCK_SIZE];
    if (!dir_load_block(dir_inode, &dir_ino, buf)) return false;

    const FFS_DirEntry* ents = (const FFS_DirEntry*)buf;
    for (int i = 0; i < FFS_DIRENTRIES_PER_BLOCK; ++i) {
        if (ents[i].inode == 0 || ents[i].name_len == 0) continue;
        if (ents[i].name_len != name_len) continue;
        bool match = true;
        for (size_t j = 0; j < name_len; ++j) {
            if (ents[i].name[j] != name[j]) {
                match = false;
                break;
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
    uint8_t buf[FFS_BLOCK_SIZE];

    if (!read_inode(dir_inode, &dir_ino)) return false;
    if (dir_ino.type != 2) return false;

    if (dir_ino.first_cluster == 0 || dir_ino.first_cluster == FFS_FAT_EOF) {
        uint64_t c = alloc_cluster();
        if (c == 0) return false;
        dir_ino.first_cluster = c;
        dir_ino.last_cluster = c;
        ffs_memzero(buf, FFS_BLOCK_SIZE);
    } else {
        if (!ffs_read_block(dir_ino.first_cluster, buf)) return false;
    }

    FFS_DirEntry* ents = (FFS_DirEntry*)buf;
    for (int i = 0; i < FFS_DIRENTRIES_PER_BLOCK; ++i) {
        if (ents[i].inode == 0 || ents[i].name_len == 0) {
            ents[i].inode = inode_num;
            ents[i].type = type;
            ents[i].name_len = (uint8_t)name_len;
            ents[i].reserved[0] = 0;
            ents[i].reserved[1] = 0;
            ffs_memzero(ents[i].name, sizeof(ents[i].name));
            for (size_t j = 0; j < name_len && j < sizeof(ents[i].name); ++j) {
                ents[i].name[j] = name[j];
            }
            dir_ino.size = (uint64_t)FFS_BLOCK_SIZE;
            if (!dir_save_block(dir_inode, &dir_ino, buf)) return false;
            return true;
        }
    }

    return false;
}

static bool dir_remove_entry(uint32_t dir_inode, const char* name, size_t name_len) {
    FFS_Inode dir_ino;
    uint8_t buf[FFS_BLOCK_SIZE];
    if (!dir_load_block(dir_inode, &dir_ino, buf)) return false;

    FFS_DirEntry* ents = (FFS_DirEntry*)buf;
    for (int i = 0; i < FFS_DIRENTRIES_PER_BLOCK; ++i) {
        if (ents[i].inode == 0 || ents[i].name_len == 0) continue;
        if (ents[i].name_len != name_len) continue;
        bool match = true;
        for (size_t j = 0; j < name_len; ++j) {
            if (ents[i].name[j] != name[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            ents[i].inode = 0;
            ents[i].type = 0;
            ents[i].name_len = 0;
            ffs_memzero(ents[i].name, sizeof(ents[i].name));
            if (!dir_save_block(dir_inode, &dir_ino, buf)) return false;
            return true;
        }
    }
    return false;
}

static bool dir_is_empty_except_dots(uint32_t dir_inode) {
    FFS_Inode dir_ino;
    uint8_t buf[FFS_BLOCK_SIZE];
    if (!dir_load_block(dir_inode, &dir_ino, buf)) return false;

    const FFS_DirEntry* ents = (const FFS_DirEntry*)buf;
    for (int i = 0; i < FFS_DIRENTRIES_PER_BLOCK; ++i) {
        if (ents[i].inode == 0 || ents[i].name_len == 0) continue;
        if (ents[i].name_len == 1 && ents[i].name[0] == '.') continue;
        if (ents[i].name_len == 2 && ents[i].name[0] == '.' && ents[i].name[1] == '.') continue;
        return false;
    }
    return true;
}

static bool dir_init_dot_entries(uint32_t dir_inode, uint32_t parent_inode) {
    FFS_Inode dir_ino;
    uint8_t buf[FFS_BLOCK_SIZE];

    if (!read_inode(dir_inode, &dir_ino)) return false;
    if (dir_ino.first_cluster == 0 || dir_ino.first_cluster == FFS_FAT_EOF) {
        uint64_t c = alloc_cluster();
        if (c == 0) return false;
        dir_ino.first_cluster = c;
        dir_ino.last_cluster = c;
    }

    ffs_memzero(buf, FFS_BLOCK_SIZE);

    FFS_DirEntry* ents = (FFS_DirEntry*)buf;

    ents[0].inode = dir_inode;
    ents[0].type = 2;
    ents[0].name_len = 1;
    ents[0].reserved[0] = 0;
    ents[0].reserved[1] = 0;
    ents[0].name[0] = '.';

    ents[1].inode = parent_inode;
    ents[1].type = 2;
    ents[1].name_len = 2;
    ents[1].reserved[0] = 0;
    ents[1].reserved[1] = 0;
    ents[1].name[0] = '.';
    ents[1].name[1] = '.';

    dir_ino.type = 2;
    dir_ino.flags = 0;
    dir_ino.size = (uint64_t)FFS_BLOCK_SIZE;

    if (!dir_save_block(dir_inode, &dir_ino, buf)) return false;
    return true;
}

static size_t k_strlen(const char* s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static uint32_t walk_path(const char* path) {
    if (!g_mounted) return 0;
    if (!path || path[0] == 0) return 0;
    if (path[0] != '/') return 0;

    uint32_t inode = g_sb.root_inode;
    const char* p = path;

    while (*p == '/') ++p;

    char name[56];

    while (*p) {
        size_t len = 0;
        while (*p && *p != '/' && len + 1 < sizeof(name)) {
            name[len++] = *p++;
        }
        name[len] = 0;

        while (*p == '/') ++p;

        if (len == 0) continue;

        if (len == 1 && name[0] == '.') {
            continue;
        }
        if (len == 2 && name[0] == '.' && name[1] == '.') {
            FFS_DirEntry ent;
            if (!dir_find_entry(inode, "..", 2, &ent)) {
                continue;
            }
            inode = ent.inode;
            continue;
        }

        FFS_DirEntry ent;
        if (!dir_find_entry(inode, name, len, &ent)) {
            return 0;
        }
        inode = ent.inode;
    }

    return inode;
}

static bool split_parent_child(const char* path,
                               char* parent, size_t parent_cap,
                               char* name, size_t name_cap) {
    if (!path || path[0] != '/') return false;
    size_t len = k_strlen(path);
    if (len < 2) return false;

    size_t last_slash = 0;
    for (size_t i = 0; i < len; ++i) {
        if (path[i] == '/') last_slash = i;
    }

    if (last_slash == len - 1) {
        len--;
        if (len < 2) return false;
        last_slash = 0;
        for (size_t i = 0; i < len; ++i) {
            if (path[i] == '/') last_slash = i;
        }
    }

    size_t parent_len = (last_slash == 0) ? 1 : last_slash;
    size_t name_len = len - last_slash - 1;

    if (parent_len + 1 > parent_cap) return false;
    if (name_len + 1 > name_cap) return false;

    if (last_slash == 0) {
        parent[0] = '/';
        parent[1] = 0;
    } else {
        for (size_t i = 0; i < parent_len; ++i) parent[i] = path[i];
        parent[parent_len] = 0;
    }

    for (size_t i = 0; i < name_len; ++i) name[i] = path[last_slash + 1 + i];
    name[name_len] = 0;
    return true;
}

static bool get_cluster_for_index(const FFS_Inode* ino, uint64_t index, uint64_t* cluster) {
    if (!ino || ino->first_cluster == 0) return false;
    uint64_t current = ino->first_cluster;
    for (uint64_t i = 0; i < index; ++i) {
        uint64_t next = FFS_FAT_EOF;
        if (!fat_read_entry(current, &next)) return false;
        if (next == FFS_FAT_EOF) return false;
        current = next;
    }
    *cluster = current;
    return true;
}

static bool ensure_cluster_index(FFS_Inode* ino, uint64_t index, uint64_t* cluster) {
    if (!ino) return false;
    if (ino->first_cluster == 0) {
        uint64_t c = alloc_cluster();
        if (c == 0) return false;
        ino->first_cluster = c;
        ino->last_cluster = c;
    }

    uint64_t current = ino->first_cluster;
    for (uint64_t i = 0; i < index; ++i) {
        uint64_t next = FFS_FAT_EOF;
        if (!fat_read_entry(current, &next)) return false;
        if (next == FFS_FAT_EOF) {
            uint64_t c = alloc_cluster();
            if (c == 0) return false;
            if (!fat_write_entry(current, c)) return false;
            next = c;
            ino->last_cluster = c;
        }
        current = next;
    }

    *cluster = current;
    return true;
}

namespace zircon_ffs {

static void ensure_dir(const char* path) {
    if (!path) return;
    if (zircon_ffs::lookup_path(path) != 0) return;
    zircon_ffs::create_dir(path);
}

static void ensure_default_layout() {
    ensure_dir("/sys");
    ensure_dir("/sys/modules");
    ensure_dir("/sys/modules/devices");
    ensure_dir("/sys/modules/mount");
    ensure_dir("/sys/firmware");
    ensure_dir("/sys/kernel");
    ensure_dir("/sys/bin");
    ensure_dir("/sys/ui-bin");
    ensure_dir("/sys/sm-bin");
    ensure_dir("/sys/sb-bin");
    ensure_dir("/sys/sb-bin/core");
    ensure_dir("/sys/sb-bin/core/clamlang");
    ensure_dir("/sys/sb-bin/core/clamshell");
    ensure_dir("/sys/sb-bin/core/clamscript");
    ensure_dir("/sys/sb-bin/core/eden");

    ensure_dir("/home");
    ensure_dir("/home/root");
    ensure_dir("/home/users");

    ensure_dir("/env");
    ensure_dir("/env/var");
    ensure_dir("/env/var/usr");
    ensure_dir("/env/var/sys");
    ensure_dir("/env/proc");
    ensure_dir("/env/tmp");

    ensure_dir("/adam");
    ensure_dir("/adam/store");
    ensure_dir("/adam/var");
    ensure_dir("/adam/tmp");
    ensure_dir("/adam/hm");
}

bool mount() {
    uint8_t buffer[FFS_BLOCK_SIZE];
    if (!ffs_read_block(0, buffer)) return false;
    const FFS_Superblock* sb = (const FFS_Superblock*)buffer;

    if (sb->magic[0] != 'F' || sb->magic[1] != 'F' ||
        sb->magic[2] != '4' || sb->magic[3] != '2') {
        return false;
    }

    if (sb->block_size != FFS_BLOCK_SIZE) return false;
    if (sb->total_blocks != FFS_TOTAL_BLOCKS) return false;

    g_sb = *sb;
    g_mounted = true;
    return true;
}

bool format() {
    ffs_memzero(&g_sb, sizeof(g_sb));

    g_sb.magic[0] = 'F';
    g_sb.magic[1] = 'F';
    g_sb.magic[2] = '4';
    g_sb.magic[3] = '2';
    g_sb.version = 2;
    g_sb.block_size = FFS_BLOCK_SIZE;
    g_sb.total_blocks = FFS_TOTAL_BLOCKS;
    g_sb.fat_start = FFS_FAT_START;
    g_sb.fat_blocks = FFS_FAT_BLOCKS;
    g_sb.inode_table_start = FFS_INODE_TABLE_START;
    g_sb.inode_table_blocks = FFS_INODE_TABLE_BLOCKS;
    g_sb.inode_count = (uint32_t)FFS_INODE_COUNT;
    g_sb.root_inode = 1;

    if (!ffs_write_block(0, &g_sb)) return false;

    uint8_t zero[FFS_BLOCK_SIZE];
    ffs_memzero(zero, FFS_BLOCK_SIZE);

    for (uint64_t b = FFS_FAT_START;
         b < FFS_FAT_START + FFS_FAT_BLOCKS + FFS_INODE_TABLE_BLOCKS;
         ++b) {
        if (!ffs_write_block(b, zero)) return false;
    }

    for (uint64_t b = 0; b < FFS_DATA_START; ++b) {
        fat_write_entry(b, FFS_FAT_EOF);
    }

    FFS_Inode root;
    ffs_memzero(&root, sizeof(root));
    root.type = 2;
    root.flags = 0;
    root.size = 0;
    root.first_cluster = 0;
    root.last_cluster = 0;

    if (!write_inode(1, &root)) return false;
    if (!dir_init_dot_entries(1, 1)) return false;

    g_mounted = true;
    return true;
}

bool init() {
    if (g_mounted) return true;
    if (mount()) {
        ensure_default_layout();
        return true;
    }
    if (!format()) return false;
    if (!mount()) return false;
    ensure_default_layout();
    return true;
}

uint32_t root_inode() {
    return g_sb.root_inode;
}

uint64_t file_size(uint32_t inode) {
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
    if (ino.type != 1) return -1;

    if (offset >= ino.size) return 0;

    uint64_t remaining = length;
    if (offset + remaining > ino.size) {
        remaining = ino.size - offset;
    }

    uint8_t* out = (uint8_t*)buffer;
    uint64_t cursor = offset;
    uint64_t total_read = 0;

    while (remaining > 0) {
        uint64_t cluster_index = cursor / FFS_BLOCK_SIZE;
        uint64_t offset_in_cluster = cursor % FFS_BLOCK_SIZE;
        uint64_t cluster = 0;
        if (!get_cluster_for_index(&ino, cluster_index, &cluster)) break;

        uint8_t block[FFS_BLOCK_SIZE];
        if (!ffs_read_block(cluster, block)) return -1;

        uint64_t chunk = FFS_BLOCK_SIZE - offset_in_cluster;
        if (chunk > remaining) chunk = remaining;

        ffs_memcpy(out + total_read, block + offset_in_cluster, chunk);

        remaining -= chunk;
        total_read += chunk;
        cursor += chunk;
    }

    return (int)total_read;
}

int write_file(uint32_t inode_num, uint64_t offset, const void* buffer, uint32_t length) {
    if (!g_mounted) return -1;
    if (length == 0) return 0;

    FFS_Inode ino;
    if (!read_inode(inode_num, &ino)) return -1;
    if (ino.type != 1) return -1;

    uint64_t end_offset = offset + length;
    uint64_t start_cluster_index = offset / FFS_BLOCK_SIZE;
    uint64_t end_cluster_index = (end_offset - 1) / FFS_BLOCK_SIZE;

    const uint8_t* in = (const uint8_t*)buffer;
    uint64_t cursor = offset;
    uint64_t written = 0;

    for (uint64_t cluster_index = start_cluster_index; cluster_index <= end_cluster_index; ++cluster_index) {
        uint64_t cluster = 0;
        if (!ensure_cluster_index(&ino, cluster_index, &cluster)) return -1;

        uint8_t block[FFS_BLOCK_SIZE];
        if (!ffs_read_block(cluster, block)) return -1;

        uint64_t offset_in_cluster = cursor % FFS_BLOCK_SIZE;
        uint64_t chunk = FFS_BLOCK_SIZE - offset_in_cluster;
        if (chunk > (length - written)) chunk = length - written;

        ffs_memcpy(block + offset_in_cluster, in + written, chunk);
        if (!ffs_write_block(cluster, block)) return -1;

        written += chunk;
        cursor += chunk;
    }

    if (end_offset > ino.size) ino.size = end_offset;
    if (!write_inode(inode_num, &ino)) return -1;

    return (int)written;
}

bool list_dir(uint32_t inode_num, void (*callback)(const FFS_DirEntry&)) {
    if (!g_mounted) return false;
    if (!callback) return false;

    FFS_Inode dir_ino;
    uint8_t buf[FFS_BLOCK_SIZE];
    if (!dir_load_block(inode_num, &dir_ino, buf)) return false;

    const FFS_DirEntry* ents = (const FFS_DirEntry*)buf;
    for (int i = 0; i < FFS_DIRENTRIES_PER_BLOCK; ++i) {
        if (ents[i].inode == 0 || ents[i].name_len == 0) continue;
        callback(ents[i]);
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

    FFS_DirEntry dummy;
    if (dir_find_entry(parent_inode, name, k_strlen(name), &dummy)) {
        return false;
    }

    uint32_t inode_num = alloc_inode();
    if (inode_num == 0) return false;

    FFS_Inode ino;
    if (!read_inode(inode_num, &ino)) return false;
    ino.type = 2;
    ino.flags = 0;
    ino.size = 0;
    ino.first_cluster = 0;
    ino.last_cluster = 0;
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

    FFS_DirEntry dummy;
    if (dir_find_entry(parent_inode, name, k_strlen(name), &dummy)) {
        return false;
    }

    uint32_t inode_num = alloc_inode();
    if (inode_num == 0) return false;

    FFS_Inode ino;
    if (!read_inode(inode_num, &ino)) return false;
    ino.type = 1;
    ino.flags = 0;
    ino.size = 0;
    ino.first_cluster = 0;
    ino.last_cluster = 0;
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
        if (!dir_is_empty_except_dots(target_inode)) return false;
    }

    if (ino.first_cluster != 0 && ino.first_cluster != FFS_FAT_EOF) {
        free_cluster_chain(ino.first_cluster);
        ino.first_cluster = 0;
        ino.last_cluster = 0;
    }

    ino.type = 0;
    ino.size = 0;
    if (!write_inode(target_inode, &ino)) return false;

    if (!dir_remove_entry(parent_inode, name, k_strlen(name))) return false;

    return true;
}

} // namespace ffs
