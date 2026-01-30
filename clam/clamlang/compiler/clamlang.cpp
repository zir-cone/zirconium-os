// src/clamlang/clamlang.cpp
#include "clamlang.h"
#include "core.h"
#include "../console.h"
#include "../fourty/ffs.h"

static void print_error(const char* msg) {
    console_write("ClamLang error: ");
    console_write(msg);
    console_write("\n");
}

bool clamlang_run_file(const char* path) {
    if (!path || !*path) {
        print_error("missing path");
        return false;
    }

    uint32_t inode = ffs::lookup_path(path);
    if (inode == 0) {
        print_error("file not found");
        return false;
    }

    uint64_t size = ffs::file_size(inode);
    if (size == 0) {
        print_error("file is empty");
        return false;
    }

    char* buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        print_error("out of memory");
        return false;
    }

    int read = ffs::read_file(inode, 0, buffer, (uint32_t)size);
    if (read <= 0) {
        free(buffer);
        print_error("failed to read file");
        return false;
    }
    buffer[read] = 0;

    int result = ccl_run_source(buffer, (size_t)read, path);
    free(buffer);
    return result == 0;
}