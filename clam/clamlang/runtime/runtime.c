#include "../compiler/core.h"
#include "../../console.h"
#include <stdarg.h>

static FILE g_stderr;
FILE* stderr = &g_stderr;

typedef struct BlockHeader {
    size_t size;
    bool free;
    struct BlockHeader* next;
} BlockHeader;

static uint8_t g_heap[1024 * 1024];
static BlockHeader* g_free_list = NULL;

static void heap_init() {
    if (g_free_list) return;
    g_free_list = (BlockHeader*)g_heap;
    g_free_list->size = sizeof(g_heap) - sizeof(BlockHeader);
    g_free_list->free = true;
    g_free_list->next = NULL;
}

static size_t align_size(size_t size) {
    const size_t align = 8;
    return (size + (align - 1)) & ~(align - 1);
}

void* malloc(size_t n) {
    if (n == 0) return NULL;
    heap_init();
    size_t size = align_size(n);
    BlockHeader* current = g_free_list;
    BlockHeader* prev = NULL;

    while (current) {
        if (current->free && current->size >= size) {
            size_t remaining = current->size - size;
            if (remaining > sizeof(BlockHeader) + 8) {
                BlockHeader* next = (BlockHeader*)((uint8_t*)current + sizeof(BlockHeader) + size);
                next->size = remaining - sizeof(BlockHeader);
                next->free = true;
                next->next = current->next;
                current->next = next;
                current->size = size;
            }
            current->free = false;
            return (uint8_t*)current + sizeof(BlockHeader);
        }
        prev = current;
        current = current->next;
    }
    (void)prev;
    return NULL;
}

void free(void* p) {
    if (!p) return;
    BlockHeader* block = (BlockHeader*)((uint8_t*)p - sizeof(BlockHeader));
    block->free = true;

    BlockHeader* current = g_free_list;
    while (current) {
        if (current->free) {
            BlockHeader* next = current->next;
            if (next && next->free &&
                (uint8_t*)current + sizeof(BlockHeader) + current->size == (uint8_t*)next) {
                current->size += sizeof(BlockHeader) + next->size;
                current->next = next->next;
                continue;
            }
        }
        current = current->next;
    }
}

void* realloc(void* p, size_t n) {
    if (!p) return malloc(n);
    if (n == 0) {
        free(p);
        return NULL;
    }

    BlockHeader* block = (BlockHeader*)((uint8_t*)p - sizeof(BlockHeader));
    size_t size = align_size(n);
    if (block->size >= size) return p;

    void* newp = malloc(n);
    if (!newp) return NULL;
    memcpy(newp, p, block->size);
    free(p);
    return newp;
}

void* memcpy(void* dst, const void* src, size_t bytes) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < bytes; ++i) {
        d[i] = s[i];
    }
    return dst;
}

void* memset(void* dst, int value, size_t bytes) {
    uint8_t* d = (uint8_t*)dst;
    for (size_t i = 0; i < bytes; ++i) {
        d[i] = (uint8_t)value;
    }
    return dst;
}

size_t strlen(const char* s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static void write_char(char c) {
    console_putc(c);
}

static void write_str(const char* s) {
    if (!s) return;
    console_write(s);
}

static void write_uint64(uint64_t v) {
    char buf[32];
    size_t i = 0;
    if (v == 0) {
        write_char('0');
        return;
    }
    while (v > 0 && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        write_char(buf[--i]);
    }
}

static void write_int64(int64_t v) {
    if (v < 0) {
        write_char('-');
        write_uint64((uint64_t)(-v));
        return;
    }
    write_uint64((uint64_t)v);
}

static int vprint(const char* fmt, va_list args) {
    int count = 0;
    for (size_t i = 0; fmt && fmt[i]; ++i) {
        if (fmt[i] != '%') {
            write_char(fmt[i]);
            ++count;
            continue;
        }
        ++i;
        if (!fmt[i]) break;

        if (fmt[i] == '%') {
            write_char('%');
            ++count;
            continue;
        }

        bool long_long = false;
        bool size_t_mod = false;
        if (fmt[i] == 'l' && fmt[i + 1] == 'l') {
            long_long = true;
            i += 2;
        } else if (fmt[i] == 'z') {
            size_t_mod = true;
            ++i;
        }

        char spec = fmt[i];
        switch (spec) {
            case 's': {
                const char* s = va_arg(args, const char*);
                write_str(s);
                break;
            }
            case 'd': {
                if (long_long) {
                    int64_t v = va_arg(args, long long);
                    write_int64(v);
                } else if (size_t_mod) {
                    size_t v = va_arg(args, size_t);
                    write_int64((int64_t)v);
                } else {
                    int v = va_arg(args, int);
                    write_int64((int64_t)v);
                }
                break;
            }
            case 'u': {
                if (long_long) {
                    uint64_t v = va_arg(args, unsigned long long);
                    write_uint64(v);
                } else if (size_t_mod) {
                    size_t v = va_arg(args, size_t);
                    write_uint64((uint64_t)v);
                } else {
                    unsigned int v = va_arg(args, unsigned int);
                    write_uint64((uint64_t)v);
                }
                break;
            }
            default:
                write_char('?');
                break;
        }
    }
    return count;
}

int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int count = vprint(fmt, args);
    va_end(args);
    return count;
}

int fprintf(FILE* stream, const char* fmt, ...) {
    (void)stream;
    va_list args;
    va_start(args, fmt);
    int count = vprint(fmt, args);
    va_end(args);
    return count;
}

static CCL_JmpBuf* g_abort_env = NULL;

int ccl_setjmp(CCL_JmpBuf* env) {
    int ret = 0;
    __asm__ volatile(
        "movl %%ebx, 0(%1)\n"
        "movl %%esi, 4(%1)\n"
        "movl %%edi, 8(%1)\n"
        "movl %%ebp, 12(%1)\n"
        "leal 4(%%esp), %%eax\n"
        "movl %%eax, 16(%1)\n"
        "movl (%%esp), %%eax\n"
        "movl %%eax, 20(%1)\n"
        "xor %%eax, %%eax\n"
        : "=a"(ret)
        : "r"(env)
        : "memory");
    return ret;
}

void ccl_longjmp(CCL_JmpBuf* env, int value) {
    if (value == 0) value = 1;
    __asm__ volatile(
        "movl 0(%0), %%ebx\n"
        "movl 4(%0), %%esi\n"
        "movl 8(%0), %%edi\n"
        "movl 12(%0), %%ebp\n"
        "movl 16(%0), %%esp\n"
        "movl %1, %%eax\n"
        "jmp *20(%0)\n"
        :
        : "r"(env), "r"(value)
        : "eax", "memory");
    __builtin_unreachable();
}

void ccl_set_abort_env(CCL_JmpBuf* env) {
    g_abort_env = env;
}

void ccl_clear_abort_env() {
    g_abort_env = NULL;
}

void exit(int code) {
    if (g_abort_env) {
        ccl_longjmp(g_abort_env, code);
    }
    (void)code;
    for (;;) {
        __asm__ volatile("hlt");
    }
}