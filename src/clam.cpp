// src/clam.cpp
#include "clam.h"
#include "fourty/ffs.h"
#include "fourty/block_device.h"
#include "ports.h"      // for io_wait if needed
#include <stdint.h>
#include <stddef.h>
#include "console.h"

// ---- External console & keyboard hooks ----
// Adjust these names if yours are different.

char keyboard_get_last_char();

// ---- Local utilities (no libc) ----

static size_t k_strlen(const char* s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

static void k_strcpy(char* dst, const char* src) {
    if (!dst || !src) return;
    size_t i = 0;
    while (src[i]) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void k_strcat(char* dst, const char* src, size_t dst_cap) {
    if (!dst || !src || dst_cap == 0) return;
    size_t dlen = k_strlen(dst);
    size_t i = 0;
    while (src[i] && dlen + i + 1 < dst_cap) {
        dst[dlen + i] = src[i];
        ++i;
    }
    dst[dlen + i] = 0;
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

static bool k_streq(const char* a, const char* b) {
    if (!a || !b) return false;
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return false;
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static bool k_starts_with(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    size_t i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return false;
        ++i;
    }
    return true;
}

// ---- Clam state ----

namespace clam {

static uint32_t g_cwd_inode = 0;
static char     g_cwd_path[256] = "/";

// ---- Low-level I/O helpers ----

static void print(const char* s) {
    console_write(s);
}

static void println(const char* s) {
    console_write(s);
    console_write("\n");
}

// read a line from keyboard, echoing, into buf (null-terminated).
// max_len includes terminator.
static void read_line(char* buf, size_t max_len) {
    size_t len = 0;
    if (max_len == 0) return;
    buf[0] = 0;

    while (1) {
        char c = 0;
        // Sleep until we actually have a key
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
        if (c == '\b' || c == 0x7F) {
            if (len > 0) {
                len--;
                buf[len] = 0;
                // erase from screen
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

// ---- Path resolution ----

// Resolve 'input' relative to current working directory into 'out'.
// out_cap is its capacity. Returns false on overflow.
static bool resolve_path(const char* input, char* out, size_t out_cap) {
    if (!input || !out || out_cap == 0) return false;

    // Absolute path
    if (input[0] == '/') {
        size_t len = k_strlen(input);
        if (len + 1 > out_cap) return false;
        for (size_t i = 0; i <= len; ++i) out[i] = input[i];
        return true;
    }

    // Relative path
    size_t cwd_len = k_strlen(g_cwd_path);
    size_t in_len  = k_strlen(input);

    bool root = (cwd_len == 1 && g_cwd_path[0] == '/');

    // Pattern:
    //   if cwd == "/", result = "/" + input
    //   else result = cwd + "/" + input
    size_t need = (root ? 1 : cwd_len + 1) + in_len + 1;
    if (need > out_cap) return false;

    size_t pos = 0;
    if (root) {
        out[0] = '/';
        pos = 1;
    } else {
        for (size_t i = 0; i < cwd_len; ++i) out[pos++] = g_cwd_path[i];
        out[pos++] = '/';
    }
    for (size_t i = 0; i < in_len; ++i) out[pos++] = input[i];
    out[pos] = 0;
    return true;
}

// Dummy callback for dir-type checks
static void noop_dir_callback(const FFS_DirEntry&) {}

// Check if an inode is a directory by trying list_dir
static bool is_dir_inode(uint32_t inode) {
    // If list_dir returns true but just doesn't call callback, it's a directory.
    return ffs::list_dir(inode, noop_dir_callback);
}

// ---- Command handlers ----

static void cmd_help() {
    println("Clamshell commands (v0):");
    println("  SAY <text>               - print text");
    println("  SAY --wd                 - print working directory");
    println("  SAY --whats-inside <p>   - print contents of file");
    println("  LDIR [path]              - list directory");
    println("  CDIR <path>              - change directory");
    println("  MAKE <name|dir/>         - create file or directory");
    println("  REMOVE <path>            - remove file (v0: non-recursive)");
    println("  CONCAT \"text\" TO file   - append text to file");
    println("  CONCAT \"text\" AS file   - overwrite file with text");
    println("  HELP                     - show this help");
}

// Basic $-string handling: for now, $"/$F" just treat escapes; ignore &x/&{}
// (we can extend later)
static void expand_basic_formatted(const char* src, char* dst, size_t dst_cap) {
    // src points *after* the leading $", i.e. starting at first char within the quotes
    // We'll process until closing " or end.
    size_t d = 0;
    size_t i = 0;
    while (src[i] && src[i] != '"' && d + 1 < dst_cap) {
        char c = src[i++];
        if (c == '\\' && src[i]) {
            char n = src[i++];
            if (n == 'n') c = '\n';
            else if (n == 't') c = '\t';
            else if (n == '\\') c = '\\';
            else if (n == '"') c = '"';
            else {
                // unknown escape, just output the character as-is
                c = n;
            }
        }
        dst[d++] = c;
    }
    dst[d] = 0;
}

// SAY command
static void handle_SAY(const char* args) {
    // skip spaces
    while (*args == ' ' || *args == '\t') ++args;

    if (*args == 0) {
        println("");
        return;
    }

    // Flags
    if (k_streq(args, "--wd") || k_streq(args, "-wd") ||
        k_streq(args, "--working-directory")) {
        println(g_cwd_path);
        return;
    }

    if (k_starts_with(args, "--whats-inside")) {
        // form: SAY --whats-inside <path>
        const char* p = args;
        while (*p && *p != ' ') ++p;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == 0) {
            println("Error: SAY --whats-inside requires a path");
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

        // Read and print (limited) file contents
        char buf[256];
        uint64_t offset = 0;
        while (true) {
            int n = ffs::read_file(inode, offset, buf, sizeof(buf) - 1);
            if (n <= 0) break;
            buf[n] = 0;
            print(buf);
            offset += (uint64_t)n;
            // safety: avoid spamming huge files endlessly (demo limitation)
            if (offset > 4096) {
                println("\n[... truncated ...]");
                break;
            }
        }
        println("");
        return;
    }

    // String literal or formatted string
    if (args[0] == '"' || (args[0] == '$' && args[1] == '"')) {
        char expanded[256];

        if (args[0] == '"') {
            // Plain string, just strip quotes, no escapes
            const char* p = args + 1;
            size_t d = 0;
            while (*p && *p != '"' && d + 1 < sizeof(expanded)) {
                expanded[d++] = *p++;
            }
            expanded[d] = 0;
        } else {
            // $"..."
            const char* p = args + 2; // after $"
            expand_basic_formatted(p, expanded, sizeof(expanded));
        }

        println(expanded);
        return;
    }

    // Default: print args as-is
    println(args);
}

// LDIR
static void handle_LDIR(const char* args) {
    // Optional path argument
    while (*args == ' ' || *args == '\t') ++args;
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

    // Callback prints each entry
    auto cb = [](const FFS_DirEntry& e) {
        console_write(e.name);
        console_write("\n");
    };

    if (!ffs::list_dir(inode, cb)) {
        println("Error: not a directory");
    }
}

// CDIR
static void handle_CDIR(const char* args) {
    while (*args == ' ' || *args == '\t') ++args;
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
    // Normalize path: we just use resolved as cwd_path
    k_strcpy(g_cwd_path, resolved);
    if (g_cwd_path[0] == 0) {
        g_cwd_path[0] = '/';
        g_cwd_path[1] = 0;
    }
}

// MAKE
static void handle_MAKE(const char* args) {
    while (*args == ' ' || *args == '\t') ++args;
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
    if (is_dir) {
        ok = ffs::create_dir(resolved);
    } else {
        ok = ffs::create_file(resolved);
    }

    if (!ok) {
        println("Error: MAKE failed (exists? invalid path?)");
    }
}

// REMOVE (v0: only files or empty dirs)
static void handle_REMOVE(const char* args) {
    while (*args == ' ' || *args == '\t') ++args;
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
        println("Error: REMOVE failed (not implemented fully yet)");
    }
}

// CONCAT "text" TO file.txt  /  CONCAT "text" AS file.txt
static void handle_CONCAT(const char* args) {
    // Expect: CONCAT "text" TO path   OR   CONCAT "text" AS path
    while (*args == ' ' || *args == '\t') ++args;
    if (*args == 0 || *args != '"') {
        println("Error: CONCAT expects a quoted string first");
        return;
    }

    // Extract text inside quotes
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
    ++p; // skip closing quote

    // Skip spaces
    while (*p == ' ' || *p == '\t') ++p;

    // Expect keyword TO or AS
    bool overwrite = false;
    if (k_starts_with(p, "TO")) {
        overwrite = false;
        p += 2;
    } else if (k_starts_with(p, "AS")) {
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

    // Ensure file exists (in v0, CREATE if missing)
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

    // im dying
    uint64_t offset = 0;
    if (!overwrite) {
        offset = ffs::file_size(inode);
    }

    if (ffs::write_file(inode, offset, text, t) < 0) {
        println("Error: write failed");
    }
}

// ---- Command dispatcher ----
// dispatch an ambulance

static void execute_line(const char* line) {
    // Skip leading whitespace
    const char* p = line;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == 0) return;        // empty line
    if (*p == '#') return;      // comment

    // Extract command name (up to first space)
    char cmd[16];
    size_t ci = 0;
    while (*p && *p != ' ' && *p != '\t' && ci + 1 < sizeof(cmd)) {
        cmd[ci++] = *p++;
    }
    cmd[ci] = 0;

    // Rest is arguments
    while (*p == ' ' || *p == '\t') ++p;
    const char* args = p;

    if (k_streq(cmd, "HELP")) {
        cmd_help();
    } else if (k_streq(cmd, "SAY")) {
        handle_SAY(args);
    } else if (k_streq(cmd, "LDIR")) {
        handle_LDIR(args);
    } else if (k_streq(cmd, "CDIR")) {
        handle_CDIR(args);
    } else if (k_streq(cmd, "MAKE")) {
        handle_MAKE(args);
    } else if (k_streq(cmd, "REMOVE")) {
        handle_REMOVE(args);
    } else if (k_streq(cmd, "CONCAT")) {
        handle_CONCAT(args);
    } else {
        print("Unknown command: ");
        println(cmd);
    }
}

// ---- Public API ----

void init() {
    g_cwd_inode = ffs::root_inode();
    g_cwd_path[0] = '/';
    g_cwd_path[1] = 0;
}

void repl() {
    char line[256];

    println("Welcome to Clamshell (v0) on ZirconiumOS.");
    println("Type HELP for a list of commands.\n");

    while (1) {
        // Prompt: cwd >
        print(g_cwd_path);
        print(" > ");

        read_line(line, sizeof(line));
        execute_line(line);
    }
}

} // namespace clam
