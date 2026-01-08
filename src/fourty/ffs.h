// src/fourty/ffs.h
#pragma once
#include <stdint.h>
#include <stddef.h>

// On-disk structures

struct __attribute__((packed)) FFS_Superblock
{
<<<<<<< HEAD
    uint8_t     magic[4];           // 'F','F','S','1'
    uint32_t    version;            // 1
    uint32_t    block_size;         // 4096
    uint32_t    total_blocks;       // 65536 for 256 MiB
    uint32_t    bitmap_start;       // first bitmap block
    uint32_t    bitmap_blocks;      // number of bitmap blocks
    uint32_t    inode_table_start;  // first inode table block
    uint32_t    inode_count;        // number of inodes
    uint32_t    root_inode;         // inode number of root directory
    uint8_t     reserved[4096
                         - 4        // magic[4]
                         - 4        // version
                         - 4        // block_size
                         - 4        // total_blocks
                         - 4        // bitmap_start
                         - 4        // bitmap_blocks
                         - 4        // inode_table_start
                         - 4        // inode_count
                         - 4];      // root_inode
};

// Extent: contiguous run of blocks; we’ll actually use 1 block per extent for now.
struct __attribute__((packed)) FFS_Extent
{
    uint32_t start_block;   // 0 = unused
    uint32_t block_count;   // number of blocks; 0 = unused
};

=======
    uint8_t     magic[4];              // 'F','F','4','2'
    uint32_t    version;               // 2
    uint32_t    block_size;            // 4096
    uint64_t    total_blocks;          // total blocks on disk
    uint64_t    fat_start;             // first FAT block
    uint64_t    fat_blocks;            // number of FAT blocks
    uint64_t    inode_table_start;     // first inode table block
    uint64_t    inode_table_blocks;    // number of inode table blocks
    uint32_t    inode_count;           // number of inodes
    uint32_t    root_inode;            // inode number of root directory
    uint8_t     reserved[4096
                         - 4           // magic[4]
                         - 4           // version
                         - 4           // block_size
                         - 8           // total_blocks
                         - 8           // fat_start
                         - 8           // fat_blocks
                         - 8           // inode_table_start
                         - 8           // inode_table_blocks
                         - 4           // inode_count
                         - 4];         // root_inode
};

// Extent: contiguous run of blocks; we’ll actually use 1 block per extent for now.
>>>>>>> 6f01370f08b307819c9bd57b453eedf4d2977a6e
struct __attribute__((packed)) FFS_Inode
{
    uint8_t     type;       // 0 = free, 1 = file, 2 = directory
    uint8_t     flags;
    uint16_t    reserved0;
    uint64_t    size;       // file size in bytes
<<<<<<< HEAD
    FFS_Extent  extents[8]; // we support up to 8 blocks (32 KiB)
=======
    uint64_t    first_cluster;
    uint64_t    last_cluster;
>>>>>>> 6f01370f08b307819c9bd57b453eedf4d2977a6e
    uint8_t     reserved[256
                         - 1       // type
                         - 1       // flags
                         - 2       // reserved0
                         - 8       // size
<<<<<<< HEAD
                         - (8 * sizeof(FFS_Extent))];
=======
                         - 8       // first_cluster
                         - 8];     // last_cluster
>>>>>>> 6f01370f08b307819c9bd57b453eedf4d2977a6e
};

struct __attribute__((packed)) FFS_DirEntry
{
    uint32_t    inode;      // 0 = unused
    uint8_t     type;       // 1 = file, 2 = dir
    uint8_t     name_len;
    uint8_t     reserved[2];
    char        name[56];   // null-terminated if name_len < 56
};

// Public API

namespace ffs
{
    bool        init();
    bool        format();
    bool        mount();
    uint32_t    root_inode();

    bool        create_file(const char* path);              // absolute path
    bool        create_dir(const char* path);               // absolute path

    uint32_t    lookup_path(const char* path);              // absolute path
    uint64_t    file_size(uint32_t inode);

    int         read_file(uint32_t inode,
                          uint64_t offset,
                          void* buffer,
                          uint32_t length);

    int         write_file(uint32_t inode,
                           uint64_t offset,
                           const void* buffer,
                           uint32_t length);

    bool        list_dir(uint32_t inode,
                         void (*callback)(const FFS_DirEntry&));

    bool        remove_path(const char* path);              // absolute path
}
