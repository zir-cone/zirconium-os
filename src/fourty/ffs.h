// src/fourty/ffs.h
#pragma once
#include <stdint.h>
#include <stddef.h>

// ondisk structures

struct __attribute__((packed)) FFS_Superblock 
{
    uint8_t     magic[4];           // 'F', 'F', 'S', '1'
    uint32_t    version;            // 1
    uint32_t    block_size;         // 4096
    uint32_t    total_blocks;       // 65536 for 256 MiB
    uint32_t    bitmap_start;       // first bitmap block
    uint32_t    bitmap_blocks;      // number of bitmap blocks
    uint32_t    inode_table_start;  // first inode table blcok
    uint32_t    inode_count;        // eg 8192
    uint32_t    root_inode;         // inode number of root directory
    uint8_t     reserved[4096 - 4 - 4 - 4 - 4
                              - 4 - 4
                              - 4 - 4
                              - 4];
};

// extent: contigupus run of blocks
struct __attribute__((packed)) FFS_Extent
{
    uint32_t start_block;   // 0 = unused
    uint32_t block_count;   // number of blocks; 0 = unused
};

struct __attribute__((packed)) FFS_Inode 
{
    uint8_t     type;
    uint8_t     flags;
    uint16_t    reserved0;
    uint64_t    size;
    FFS_Extent  extents[8];
    uint8_t     reserved[256 
                         - 1 
                         - 1 
                         - 2 
                         - 8 
                         - (8 * sizeof(FFS_Extent))];
};

struct __attribute__((packed)) FFS_DirEntry
{
    uint32_t    inode;      // 0 = unused
    uint8_t     type;       // 1 = file, 2 = dir
    uint8_t     name_len;
    uint8_t     reserved[2];
    char        name[56];   // null-terminated if name_len < 56    
};

// public api

namespace ffs // its 4 am im so tired 😭
{

    bool        init();
    bool        format();
    bool        mount();
    uint32_t    root_inode();
    bool        create_file(const char* path);
    bool        create_dir(const char* path);
    uint32_t    lookup_path(const char* path);
    uint64_t    file_size(uint32_t inode);
    int         read_file(uint32_t inode, uint64_t offset, void* buffer, uint32_t length);
    int         write_file(uint32_t inode, uint64_t offset, const void* buffer, uint32_t length);
    bool        list_dir(uint32_t inode, void (*callback)(const FFS_DirEntry&));
    bool        remove_path(const char* path);
} // namespace ffs
