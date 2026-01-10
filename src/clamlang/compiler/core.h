#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================= Diagnostics ================= */

typedef struct {
    const char* file;
    const char* src;
    size_t      src_len;
} Source;

static void die_at(const Source* S, int line, int col, const char* kind, const char* msg) {
    fprintf(stderr, "%s: %s at %s:%d:%d\n", kind, msg, S->file ? S->file : "<input>", line, col);
    exit(1);
}

static void* xmalloc(size_t n) {
    void* p = malloc(n);
    if (!p) { fprintf(stderr, "fatal: out of memory\n"); exit(1); }
    return p;
}

static void* xrealloc(void* p, size_t n) {
    void* q = realloc(p, n);
    if (!q) { fprintf(stderr, "fatal: out of memory\n"); exit(1); }
    return q;
}

static char* xstrdup(const char* s) {
    size_t n = strlen(s);
    char* r = (char*)xmalloc(n+1);
    memcpy(r, s, n+1);
    return r;
}

/* ================= Lexer ================= */

typedef enum {
    TK_EOF = 0,

    TK_IDENT,
    TK_INT_LIT,
    TK_STR_LIT,

    TK_LPAREN, TK_RPAREN,
    TK_LBRACE, TK_RBRACE,
    TK_LBRACKET, TK_RBRACKET,
    TK_COMMA,
    TK_SEMI,

    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH,
    TK_PERCENT,
    TK_EQ,
    TK_EQEQ, TK_NEQ,
    TK_PLUSEQ, TK_MINUSEQ, TK_STAREQ, TK_SLASHEQ,
    TK_PERCENTEQ,
    TK_LT, TK_LTE, TK_GT, TK_GTE,

    TK_ARROW,
    TK_RARROW,
    TK_PLUSPLUS,
    TK_MINUSMINUS,

    TK_ELLIPSIS,

    // Keywords (case-insensitive)
    TK_DEFINE,
    TK_FUNCTION,
    TK_CLASS,

    TK_LET,
    TK_SET,
    TK_VOID,
    TK_VOIDLET,
    TK_SLET,

    TK_RETURN,
    TK_NOTHING,

    TK_IF,
    TK_ELSE,

    TK_FOR,
    TK_WHILE,
    TK_WITHIN,

    TK_SAY,

    TK_AND,
    TK_OR,
    TK_NOT,
    TK_NAND,
    TK_NOR,

    TK_NULL,
    TK_TRUE,
    TK_FALSE,

    TK_PUBLIC,
    TK_PRIVATE,

    TK_AT_IDENT
} TokenKind;

typedef struct {
    TokenKind kind;
    size_t    start;
    size_t    end;
    int       line;
    int       col;

    // true if no whitespace/comment between this token and the next token
    bool      adjacent_to_next;
} Token;

typedef struct {
    Source S;
    size_t i;
    int line;
    int col;

    Token* toks;
    size_t ntoks;
    size_t cap;
} Lexer;

void lex_all(Lexer* L);

/* ================= Types ================= */

typedef enum { TY_NONE=0, TY_INT, TY_BOOL, TY_STR, TY_NULL, TY_ANY, TY_LIST } TypeTag;

typedef struct {
    bool    is_typed;
    bool    is_const_binding; // const variable binding (cannot SET)
    TypeTag tag;              // if typed
} TypeSpec;

typedef struct Expr Expr;

typedef enum { TGT_NAME=1, TGT_INDEX=2 } TargetKind;

typedef struct {
    TargetKind kind;
    int line, col;
    union {
        struct { char* name; } name;
        struct { char* base_name; Expr* index; } index; // base must be var
    } as;
} Target;

/* ================= AST ================= */

typedef enum {
    EX_INT=1,
    EX_BOOL,
    EX_STR,
    EX_NULL,
    EX_VAR,
    EX_BIN,
    EX_CALL,
    EX_INDEX,
    EX_UNARY,
    EX_LIST
} ExprKind;

typedef struct Expr Expr;
struct Expr {
    ExprKind kind;
    int line, col;
    union {
        int64_t ival;
        bool    bval;
        char*   sval; // raw string without quotes
        struct { char* name; } var;
        struct { int op; Expr* a; Expr* b; } bin;
        struct { int op; Expr* inner; } unary;
        struct { char* callee; Expr** args; size_t argc; } call;
        struct { Expr* base; Expr* index; } index;
        struct { Expr** items; size_t count; } list;
    } as;
};

typedef enum {
    ST_LET=1,
    ST_SET,
    ST_INCDEC,
    ST_VOID,
    ST_VOIDLET,
    ST_SLET,
    ST_SAY_IDENT,
    ST_SAY_EXPR,
    ST_NOTHING,
    ST_RETURN,
    ST_IF,
    ST_WHILE,
    ST_FOR,
    ST_EXPR,
    ST_INCDEC
} StmtKind;

typedef enum {
    ASG_EQ = 1, // =
    ASG_ADD,    // +=
    ASG_SUB,    // -=
    ASG_MUL,    // *=
    ASG_DIV,    // /=
    ASG_MOD     // %=
    // ASG_FLOORDIV, // //=
} AssignOp;

typedef enum {
    TGT_NAME = 1,
    TGT_INDEX 
} TargetKind;

typedef struct {
    TargetKind kind;
    int line, col;
    union {
        struct {
            char* name;
        } name;
        struct {
            char* base;
            Expr* index;
        } index;
    } as;
} Target;

typedef struct Stmt Stmt;
struct Stmt {
    StmtKind kind;
    int line, col;
    Stmt* next;
    union {
        struct { char* name; TypeSpec ts; Expr* rhs; } let_stmt;                 // LET/VOIDLET/SLET
        struct { char* name; Target target; AssignOp op; Expr* rhs; } set_stmt;  // SET
        struct { char* name; } void_stmt;                                        // VOID
        struct { char* name; } say_ident;                                        // SAY x;
        struct { Expr* expr; } say_expr;                                         // SAY(expr);
        struct { Expr* expr; } ret_stmt;                                         // RETURN expr;
        // struct { Target target; bool is_inc; } incdec_stmt;                      // INC/DEC

        struct {
            Expr* cond;
            Stmt* then_block;
            Stmt* else_branch; // either NULL, or ST_IF, or a block list
            bool  else_is_if;
        } if_stmt;

        struct {
            Expr* cond;
            Stmt* body;
        } while_stmt;

        struct {
            // FOR ( init ; WITHIN limit ; step ) { body }
            Stmt* init;       // may be NULL
            char* iter_name;  // required for WITHIN compare
            Expr* limit;      // required
            Stmt* step;       // may be NULL (usually a SET)
            Stmt* body;
        } for_stmt;

        struct { Expr* expr; } expr_stmt;                            // expression as statement
    } as;
};

typedef struct {
    char*    name;

    // parameters
    char**    param_names;    // fixed params
    TypeSpec* param_ts;
    size_t    param_count;

    // varargs
    bool      has_varargs;
    TypeSpec  varargs_ts;     // typing for each extra arg
    char*     varargs_name;   // always ...

    // return type
    TypeTag   ret_type;

    // body
    Stmt*     body;

    // special empty-body exemption: (Null) only
    bool      takes_only_null;

    int line, col;
} FunctionDef;

typedef struct {
    FunctionDef* funcs;
    size_t nfuncs;

    Stmt* top; // top-level statements (module body)
} Program;

/* ================= Parser ================= */

typedef struct {
    Source S;
    Token* toks;
    size_t ntoks;
    size_t pos;
} Parser;

Program parse_program(Parser* P);

/* ================= VM + GC ================= */

typedef enum { OBJ_STR=1, OBJ_LIST=2 } ObjKind;

typedef struct Obj {
    ObjKind kind;
    bool marked;
    struct Obj* next;
} Obj;

typedef struct {
    Obj obj;
    size_t len;
    char* chars;
} ObjStr;

typedef struct Value Value;

typedef struct {
    Obj obj;
    size_t len, cap;
    Value* items;
} ObjList;

typedef enum { V_NULL=0, V_INT=1, V_BOOL=2, V_OBJ=3 } ValueTag;

struct Value {
    ValueTag tag;
    union { int64_t i; bool b; Obj* obj; } as;
};

static inline Value VNull(void){ Value v; v.tag=V_NULL; v.as.i=0; return v; }
static inline Value VInt(int64_t x){ Value v; v.tag=V_INT; v.as.i=x; return v; }
static inline Value VBool(bool b){ Value v; v.tag=V_BOOL; v.as.b=b; return v; }
static inline Value VObj(Obj* o){ Value v; v.tag=V_OBJ; v.as.obj=o; return v; }

typedef enum {
    OP_HALT=0,

    OP_PUSH_INT,
    OP_PUSH_BOOL,
    OP_PUSH_NULL,
    OP_PUSH_STR,      // u16 string pool index

    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_VOID_LOCAL,    // u16 local index (unbind name)

    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,

    OP_EQEQ, OP_NEQ,
    OP_LT, OP_LTE, OP_GT, OP_GTE,

    OP_NOT,
    OP_AND, OP_OR, OP_NAND, OP_NOR,

    OP_JMP,          // i32 relative
    OP_JMP_IF_FALSE, // i32 relative

    OP_CALL,         // u16 func_index, u16 argc
    OP_CALL_BUILTIN, // u16 builtin_id, u16 argc

    OP_MAKE_LIST,    // u16 count
    OP_INDEX_GET,    // (list, index) -> value
    OP_INDEX_SET,

    OP_POP,
    OP_RET
} Op;

typedef enum {
    BI_SAY    = 1,
    BI_LENGTH = 2,
    BI_TYPE   = 3,
    BI_PUSH   = 4,
    BI_POP    = 5,
    BI_RANGE  = 6
} BuiltinId;

typedef struct {
    uint8_t* code;
    size_t   len;
    size_t   cap;

    // locals metadata
    TypeSpec* locals_ts;
    char**    locals_name;    // slot name at definition time
    bool*     locals_is_live; // slot still bound to a name?
    size_t    nlocals;
    size_t    locals_cap;

    // name->slot mapping for emission time
    char**    name_map;
    uint16_t* slot_map;
    size_t    nmap;
    size_t    map_cap;
} Chunk;

typedef struct {
    // constants
    char** str_pool;
    size_t str_count;
    size_t str_cap;

    // functions
    Chunk* funcs;
    char** func_names;
    TypeTag* func_ret;

    TypeSpec** func_param_ts;
    size_t* func_param_count;

    // varargs
    bool* func_has_varargs;
    TypeSpec* func_varargs_ts;
    size_t* func_fixed_param_count;

    size_t func_count;

    // module (top-level) chunk
    Chunk module;
} Bytecode;

Bytecode emit_bytecode(const Program* prog, const Source* S);

/* ================= VM ================= */

typedef struct {
    Chunk*  chunk;
    size_t  ip;

    Value*  locals;
    size_t  nlocals;

    bool    is_function;
    uint16_t func_index;   // for return typechecking
} Frame;

typedef struct {
    const Source* S;
    Bytecode bc;

    Value* stack;
    size_t sp;
    size_t stack_cap;

    Frame* frames;
    size_t fp;
    size_t frame_cap;

    // GC
    Obj* objects;
    size_t bytes_allocated;
    size_t next_gc;
} VM;

// ------------- runtime error jmp ---------------

typedef struct {
    uint32_t ebx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t eip;
} CCL_JmpBuf;

int ccl_setjmp(CCL_JmpBuf* env);
void ccl_longjmp(CCL_JmpBuf* env, int value);
void ccl_set_abort_env(CCL_JmpBuf* env);
void ccl_clear_abort_env();

int ccl_run_source(const char* source, size_t len, const char* file_label);

void vm_run_module(VM* vm);

#ifdef __cplusplus
}
#endif