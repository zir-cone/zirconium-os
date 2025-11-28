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

static const char* HOME_PATH = "/Users/default";

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

// ------------ path resolution (., .., ~, Z:/) ------------

static bool is_alpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// Build a canonical absolute path into out ("/foo/bar").
// Supports:
//   . and ..
//   ~ or ~/ -> /Users/default
//   Z:/foo/bar -> /foo/bar
static bool resolve_path(const char* input, char* out, size_t out_cap) {
    if (!input || !out || out_cap == 0) return false;

    char combined[256];
    combined[0] = 0;

    const char* p = input;

    // Strip leading spaces
    while (*p == ' ' || *p == '\t') ++p;

    // 1) Tilde: ~ or ~/
    if (*p == '~' && (p[1] == 0 || p[1] == '/' || p[1] == '\\')) {
        const char* rest = p + 1;
        if (*rest == '/' || *rest == '\\') ++rest;

        size_t home_len = k_strlen(HOME_PATH);
        size_t rest_len = k_strlen(rest);
        if (home_len + 1 + rest_len + 1 > sizeof(combined)) return false;

        size_t pos = 0;
        for (size_t i = 0; i < home_len; ++i) combined[pos++] = HOME_PATH[i];
        if (rest_len > 0 && combined[home_len - 1] != '/') {
            combined[pos++] = '/';
        }
        for (size_t i = 0; i < rest_len; ++i) combined[pos++] = rest[i];
        combined[pos] = 0;
    }
    // 2) Windows-style drive: Z:/foo/bar
    else if (is_alpha(p[0]) && p[1] == ':' && (p[2] == '/' || p[2] == '\\')) {
        const char* rest = p + 3;
        size_t rest_len = k_strlen(rest);
        if (1 + rest_len + 1 > sizeof(combined)) return false;

        size_t pos = 0;
        combined[pos++] = '/';
        for (size_t i = 0; i < rest_len; ++i) combined[pos++] = rest[i];
        combined[pos] = 0;
    }
    // 3) Already absolute: /foo/bar
    else if (*p == '/') {
        size_t len = k_strlen(p);
        if (len + 1 > sizeof(combined)) return false;
        for (size_t i = 0; i <= len; ++i) combined[i] = p[i];
    }
    // 4) Relative: cwd + "/" + input
    else {
        size_t cwd_len   = k_strlen(g_cwd_path);
        size_t rel_len   = k_strlen(p);
        bool   root_cwd  = (cwd_len == 1 && g_cwd_path[0] == '/');

        size_t need = (root_cwd ? 1 : cwd_len + 1) + rel_len + 1;
        if (need > sizeof(combined)) return false;

        size_t pos = 0;
        if (root_cwd) {
            combined[pos++] = '/';
        } else {
            for (size_t i = 0; i < cwd_len; ++i) combined[pos++] = g_cwd_path[i];
            combined[pos++] = '/';
        }
        for (size_t i = 0; i < rel_len; ++i) combined[pos++] = p[i];
        combined[pos] = 0;
    }

    // Canonicalize: split into segments, process . and ..
    const char* q = combined;
    // ensure it starts with '/'
    if (*q != '/') {
        // shouldn't happen, but be safe
        out[0] = '/';
        out[1] = 0;
        return true;
    }

    // skip leading '/'
    while (*q == '/') ++q;

    const char* seg_start[32];
    int         seg_len[32];
    int         seg_count = 0;

    while (*q) {
        while (*q == '/') ++q;
        if (*q == 0) break;

        const char* start = q;
        int len = 0;
        while (*q && *q != '/') {
            ++q; ++len;
        }

        if (len == 0) continue;

        // "."
        if (len == 1 && start[0] == '.') {
            continue;
        }
        // ".."
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (seg_count > 0) --seg_count;
            continue;
        }

        if (seg_count < 32) {
            seg_start[seg_count] = start;
            seg_len[seg_count]   = len;
            ++seg_count;
        } else {
            return false;
        }
    }

    // Build output
    size_t pos = 0;
    if (seg_count == 0) {
        if (out_cap < 2) return false;
        out[0] = '/';
        out[1] = 0;
        return true;
    }

    for (int i = 0; i < seg_count; ++i) {
        if (pos + 1 + (size_t)seg_len[i] + 1 > out_cap) return false;
        out[pos++] = '/';
        for (int j = 0; j < seg_len[i]; ++j) {
            out[pos++] = seg_start[i][j];
        }
    }
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

    // $-strings (basic version)
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
        // Skip . and ..
        if (e.name_len == 1 && e.name[0] == '.') return;
        if (e.name_len == 2 && e.name[0] == '.' && e.name[1] == '.') return;

        // name[] may not be null-terminated, use name_len
        char name[57];
        size_t len = (e.name_len < 56) ? e.name_len : 56;
        for (size_t i = 0; i < len; ++i) name[i] = e.name[i];
        name[len] = 0;

        console_write(name);
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

    char resolved[256];
    if (!resolve_path(args, resolved, sizeof(resolved))) {
        println("Error: path too long");
        return;
    }

    size_t len = k_strlen(resolved);
    bool is_dir = (len > 0 && resolved[len - 1] == '/');

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
    println("  MAKE <file|dir/>");
    println("  REMOVE <path>");
    println("  CONCAT \"text\" TO <file>");
    println("  CONCAT \"text\" AS <file>");
    println("Path features: '.', '..', '~' -> /Users/default, and Z:/foo maps to /foo.");
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
    // kernel should already have called ffs::init()
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