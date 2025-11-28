// src/clam.cpp
#include "clam.h"
#include "fourty/ffs.h"
#include "console.h"
#include "ports.h"
#include <stdint.h>
#include <stddef.h>

// keyboard_get_last_char provided by keyboard.cpp
char keyboard_get_last_char();

// ------------ small libc-ish helpers ------------

static size_t k_strlen(const char* s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static int k_stricmp(const char* a, const char* b) {
    // case-insensitive compare
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        ++a; ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static bool k_streq_nocase(const char* a, const char* b) {
    return k_stricmp(a, b) == 0;
}

static void k_strcpy(char* dst, const char* src) {
    if (!dst || !src) return;
    while (*src) {
        *dst++ = *src++;
    }
    *dst = 0;
}

// ------------ Clam state ------------

namespace clam {

static uint32_t g_cwd_inode = 0;
static char     g_cwd_path[256] = "/";
static bool     g_ffs_ready = false;

// ------------ console helpers ------------

static void print(const char* s) {
    console_write(s);
}

static void println(const char* s) {
    console_write(s);
    console_write("\n");
}

// ------------ keyboard line input ------------

static void read_line(char* buf, size_t max_len) {
    size_t len = 0;
    if (max_len == 0) return;
    buf[0] = 0;

    while (1) {
        char c = 0;
        // block until we get a char
        while ((c = keyboard_get_last_char()) == 0) {
            asm volatile("hlt");
        }

        // Enter
        if (c == '\n' || c == '\r') {
            console_write("\n");
            buf[len] = 0;
            return;
        }

        // Backspace
        if (c == '\b' || (unsigned char)c == 0x7F) {
            if (len > 0) {
                --len;
                buf[len] = 0;
                console_write("\b \b");
            }
            continue;
        }

        // Printable ASCII
        if (c >= 32 && c < 127) {
            if (len + 1 < max_len) {
                buf[len++] = c;
                buf[len] = 0;
                char tmp[2] = { c, 0 };
                console_write(tmp);
            }
        }
    }
}

// ------------ path resolution ------------

// absolute if starting with '/', otherwise relative to g_cwd_path
static bool resolve_path(const char* input, char* out, size_t out_cap) {
    if (!input || !out || out_cap == 0) return false;

    // Absolute
    if (input[0] == '/') {
        size_t len = k_strlen(input);
        if (len + 1 > out_cap) return false;
        for (size_t i = 0; i <= len; ++i) out[i] = input[i];
        return true;
    }

    // Relative
    size_t cwd_len = k_strlen(g_cwd_path);
    size_t in_len  = k_strlen(input);

    bool root = (cwd_len == 1 && g_cwd_path[0] == '/');
    size_t need = (root ? 1 : cwd_len + 1) + in_len + 1;
    if (need > out_cap) return false;

    size_t pos = 0;
    if (root) {
        out[pos++] = '/';
    } else {
        for (size_t i = 0; i < cwd_len; ++i) out[pos++] = g_cwd_path[i];
        out[pos++] = '/';
    }
    for (size_t i = 0; i < in_len; ++i) out[pos++] = input[i];
    out[pos] = 0;
    return true;
}

// dummy callback used for dir check
static void noop_dir_callback(const FFS_DirEntry&) {}

static bool is_dir_inode(uint32_t inode) {
    return ffs::list_dir(inode, noop_dir_callback);
}

// ------------ SAY command ------------

static void handle_SAY(const char* args) {
    while (*args == ' ' || *args == '\t') ++args;

    if (*args == 0) {
        println("");
        return;
    }

    // SAY --wd
    if (k_streq_nocase(args, "--wd") ||
        k_streq_nocase(args, "-wd") ||
        k_streq_nocase(args, "--working-directory")) {
        println(g_cwd_path);
        return;
    }

    // SAY --whats-inside path
    const char* flag = "--whats-inside";
    size_t flag_len = k_strlen(flag);
    bool match_flag = true;
    for (size_t i = 0; i < flag_len; ++i) {
        char a = args[i];
        char b = flag[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (a != b) { match_flag = false; break; }
    }
    if (match_flag && (args[flag_len] == 0 || args[flag_len] == ' ' || args[flag_len] == '\t')) {
        const char* p = args + flag_len;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == 0) {
            println("Error: SAY --whats-inside requires a path");
            return;
        }
        if (!g_ffs_ready) {
            println("Error: FFS not ready");
            return;
        }
        char resolved[256];
        if (!resolve_path(p, resolved, sizeof(resolved))) {
            println("Error: path too long");
            return;
        }
        uint32_t inode = ffs::lookup_path(resolved);
        if (inode == 0) {
            println("Error: file not found");
            return;
        }

        char buf[256];
        uint64_t offset = 0;
        while (true) {
            int n = ffs::read_file(inode, offset, buf, sizeof(buf) - 1);
            if (n <= 0) break;
            buf[n] = 0;
            print(buf);
            offset += (uint64_t)n;
            if (offset > 4096) {
                println("\n[... truncated ...]");
                break;
            }
        }
        println("");
        return;
    }

    // simple string literal: SAY "text"
    if (args[0] == '"' ) {
        char out[256];
        const char* p = args + 1;
        size_t d = 0;
        while (*p && *p != '"' && d + 1 < sizeof(out)) {
            out[d++] = *p++;
        }
        out[d] = 0;
        println(out);
        return;
    }

    // $-strings (for now just treat like normal)
    if (args[0] == '$' && args[1] == '"') {
        char out[256];
        const char* p = args + 2;
        size_t d = 0;
        while (*p && *p != '"' && d + 1 < sizeof(out)) {
            char c = *p++;
            if (c == '\\' && *p) {
                char n = *p++;
                if (n == 'n') c = '\n';
                else if (n == 't') c = '\t';
                else if (n == '\\') c = '\\';
                else if (n == '"') c = '"';
                else c = n;
            }
            out[d++] = c;
        }
        out[d] = 0;
        println(out);
        return;
    }

    // default: just echo
    println(args);
}

// ------------ LDIR ------------

static void handle_LDIR(const char* args) {
    while (*args == ' ' || *args == '\t') ++args;

    if (!g_ffs_ready) {
        println("Error: FFS not ready");
        return;
    }

    uint32_t inode = g_cwd_inode;
    char resolved[256];

    if (*args != 0) {
        if (!resolve_path(args, resolved, sizeof(resolved))) {
            println("Error: path too long");
            return;
        }
        inode = ffs::lookup_path(resolved);
        if (inode == 0) {
            println("Error: directory not found");
            return;
        }
    }

    auto cb = [](const FFS_DirEntry& e) {
        console_write(e.name);
        console_write("\n");
    };

    if (!ffs::list_dir(inode, cb)) {
        println("Error: not a directory");
    }
}

// ------------ CDIR ------------

static void handle_CDIR(const char* args) {
    while (*args == ' ' || *args == '\t') ++args;

    if (!g_ffs_ready) {
        println("Error: FFS not ready");
        return;
    }

    if (*args == 0) {
        println("Error: CDIR requires a path");
        return;
    }

    char resolved[256];
    if (!resolve_path(args, resolved, sizeof(resolved))) {
        println("Error: path too long");
        return;
    }
    uint32_t inode = ffs::lookup_path(resolved);
    if (inode == 0) {
        println("Error: directory not found");
        return;
    }
    if (!is_dir_inode(inode)) {
        println("Error: not a directory");
        return;
    }

    g_cwd_inode = inode;
    k_strcpy(g_cwd_path, resolved);
    if (g_cwd_path[0] == 0) {
        g_cwd_path[0] = '/';
        g_cwd_path[1] = 0;
    }
}

// ------------ MAKE ------------

static void handle_MAKE(const char* args) {
    while (*args == ' ' || *args == '\t') ++args;
    if (!g_ffs_ready) {
        println("Error: FFS not ready");
        return;
    }
    if (*args == 0) {
        println("Error: MAKE requires a name");
        return;
    }

    char name[128];
    size_t i = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t' && i + 1 < sizeof(name)) {
        name[i] = args[i];
        ++i;
    }
    name[i] = 0;

    bool is_dir = false;
    size_t len = k_strlen(name);
    if (len > 0 && name[len - 1] == '/') {
        is_dir = true;
    }

    char resolved[256];
    if (!resolve_path(name, resolved, sizeof(resolved))) {
        println("Error: path too long");
        return;
    }

    bool ok = false;
    if (is_dir) ok = ffs::create_dir(resolved);
    else        ok = ffs::create_file(resolved);

    if (!ok) {
        println("Error: MAKE failed");
    }
}

// ------------ REMOVE ------------

static void handle_REMOVE(const char* args) {
    while (*args == ' ' || *args == '\t') ++args;
    if (!g_ffs_ready) {
        println("Error: FFS not ready");
        return;
    }
    if (*args == 0) {
        println("Error: REMOVE requires a path");
        return;
    }

    char resolved[256];
    if (!resolve_path(args, resolved, sizeof(resolved))) {
        println("Error: path too long");
        return;
    }

    if (!ffs::remove_path(resolved)) {
        println("Error: REMOVE failed");
    }
}

// ------------ CONCAT ------------

static void handle_CONCAT(const char* args) {
    while (*args == ' ' || *args == '\t') ++args;
    if (!g_ffs_ready) {
        println("Error: FFS not ready");
        return;
    }
    if (*args == 0 || *args != '"') {
        println("Error: CONCAT expects a quoted string first");
        return;
    }

    char text[256];
    const char* p = args + 1;
    size_t t = 0;
    while (*p && *p != '"' && t + 1 < sizeof(text)) {
        text[t++] = *p++;
    }
    text[t] = 0;
    if (*p != '"') {
        println("Error: unterminated string in CONCAT");
        return;
    }
    ++p;

    while (*p == ' ' || *p == '\t') ++p;
    bool overwrite = false;
    if ((p[0] == 'T' || p[0] == 't') && (p[1] == 'O' || p[1] == 'o')) {
        overwrite = false;
        p += 2;
    } else if ((p[0] == 'A' || p[0] == 'a') && (p[1] == 'S' || p[1] == 's')) {
        overwrite = true;
        p += 2;
    } else {
        println("Error: CONCAT expects TO or AS");
        return;
    }

    while (*p == ' ' || *p == '\t') ++p;
    if (*p == 0) {
        println("Error: CONCAT requires a target path");
        return;
    }

    char resolved[256];
    if (!resolve_path(p, resolved, sizeof(resolved))) {
        println("Error: path too long");
        return;
    }

    uint32_t inode = ffs::lookup_path(resolved);
    if (inode == 0) {
        if (!ffs::create_file(resolved)) {
            println("Error: failed to create target file");
            return;
        }
        inode = ffs::lookup_path(resolved);
        if (inode == 0) {
            println("Error: internal error after CREATE");
            return;
        }
    }

    uint64_t offset = 0;
    if (!overwrite) {
        offset = ffs::file_size(inode);
    }

    if (ffs::write_file(inode, offset, text, (uint32_t)t) < 0) {
        println("Error: write failed");
    }
}

// ------------ HELP ------------

static void cmd_help() {
    println("Clamshell commands:");
    println("  HELP");
    println("  SAY <text>");
    println("  SAY --wd");
    println("  SAY --whats-inside <path>");
    println("  LDIR [path]");
    println("  CDIR <path>");
    println("  MAKE <name|dir/>");
    println("  REMOVE <path>");
    println("  CONCAT \"text\" TO <file>");
    println("  CONCAT \"text\" AS <file>");
}

// ------------ command dispatcher ------------

static void execute_line(const char* line) {
    const char* p = line;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == 0) return;
    if (*p == '#') return;

    char cmd[16];
    size_t ci = 0;
    while (*p && *p != ' ' && *p != '\t' && ci + 1 < sizeof(cmd)) {
        cmd[ci++] = *p++;
    }
    cmd[ci] = 0;
    while (*p == ' ' || *p == '\t') ++p;
    const char* args = p;

    if (k_streq_nocase(cmd, "HELP")) {
        cmd_help();
    } else if (k_streq_nocase(cmd, "SAY")) {
        handle_SAY(args);
    } else if (k_streq_nocase(cmd, "LDIR")) {
        handle_LDIR(args);
    } else if (k_streq_nocase(cmd, "CDIR")) {
        handle_CDIR(args);
    } else if (k_streq_nocase(cmd, "MAKE")) {
        handle_MAKE(args);
    } else if (k_streq_nocase(cmd, "REMOVE")) {
        handle_REMOVE(args);
    } else if (k_streq_nocase(cmd, "CONCAT")) {
        handle_CONCAT(args);
    } else {
        print("Unknown command: ");
        println(cmd);
    }
}

// ------------ public API ------------

void init() {
    // kernel already called ffs::init(), but we can sanity check
    g_cwd_path[0] = '/';
    g_cwd_path[1] = 0;

    g_cwd_inode = ffs::root_inode();
    if (g_cwd_inode == 0) {
        g_ffs_ready = false;
        println("Warning: FFS root inode is 0; filesystem not ready.");
    } else {
        g_ffs_ready = true;
    }
}

void repl() {
    println("Welcome to Clamshell on ZirconiumOS.");
    println("Type HELP for a list of commands.\n");

    char line[256];
    while (1) {
        print(g_cwd_path);
        print(" > ");
        read_line(line, sizeof(line));
        execute_line(line);
    }
}

} // namespace clam
