#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CCL_TOKEN_EOF = 0,
    CCL_TOKEN_ERROR,
    CCL_TOKEN_IDENTIFIER,
    CCL_TOKEN_NUMBER,
    CCL_TOKEN_STRING_RAW,
    CCL_TOKEN_STRING_ESC,
    CCL_TOKEN_STRING_FMT,
    CCL_TOKEN_SEMICOLON,
    CCL_TOKEN_COMMA,
    CCL_TOKEN_LPAREN,
    CCL_TOKEN_RPAREN,
    CCL_TOKEN_LBRACE,
    CCL_TOKEN_RBRACE,
    CCL_TOKEN_LBRACKET,
    CCL_TOKEN_RBRACKET,
    CCL_TOKEN_PLUS,
    CCL_TOKEN_MINUS,
    CCL_TOKEN_STAR,
    CCL_TOKEN_SLASH,
    CCL_TOKEN_PERCENT,
    CCL_TOKEN_EQUAL,
    CCL_TOKEN_EQUAL_EQUAL,
    CCL_TOKEN_BANG,
    CCL_TOKEN_BANG_EQUAL,
    CCL_TOKEN_LT,
    CCL_TOKEN_LT_EQUAL,
    CCL_TOKEN_GT,
    CCL_TOKEN_GT_EQUAL,
    CCL_TOKEN_PLUS_EQUAL,
    CCL_TOKEN_MINUS_EQUAL,
    CCL_TOKEN_STAR_EQUAL,
    CCL_TOKEN_SLASH_EQUAL,
    CCL_TOKEN_ARROW,
    CCL_TOKEN_SHIFT_RIGHT,
    CCL_TOKEN_PLUS_PLUS,
    CCL_TOKEN_MINUS_MINUS,
    CCL_TOKEN_AND,
    CCL_TOKEN_OR,
    CCL_TOKEN_NOT,
    CCL_TOKEN_LET,
    CCL_TOKEN_SET,
    CCL_TOKEN_VOID,
    CCL_TOKEN_VOIDLET,
    CCL_TOKEN_SLET,
    CCL_TOKEN_IF,
    CCL_TOKEN_ELSE,
    CCL_TOKEN_WHILE,
    CCL_TOKEN_FOR,
    CCL_TOKEN_WITHIN,
    CCL_TOKEN_DEFINE,
    CCL_TOKEN_FUNCTION,
    CCL_TOKEN_RETURN,
    CCL_TOKEN_IMPORT,
    CCL_TOKEN_CLASS,
    CCL_TOKEN_PUBLIC,
    CCL_TOKEN_PRIVATE,
    CCL_TOKEN_INIT,
    CCL_TOKEN_TRUE,
    CCL_TOKEN_FALSE,
    CCL_TOKEN_NULL,
    CCL_TOKEN_SAY,
    CCL_TOKEN_LENGTH,
    CCL_TOKEN_RANGE
} CCL_TokenType;

typedef struct {
    CCL_TokenType type;
    const char* start;
    uint32_t length;
    uint32_t line;
} CCL_Token;

typedef struct {
    const char* cursor;
    uint32_t line;
} CCL_Lexer;

typedef enum {
    CCL_EXPR_NONE = 0,
    CCL_EXPR_LITERAL,
    CCL_EXPR_IDENTIFIER,
    CCL_EXPR_BINARY,
    CCL_EXPR_CALL,
    CCL_EXPR_INDEX
} CCL_ExprType;

typedef struct {
    CCL_ExprType type;
    uint32_t left;
    uint32_t right;
    CCL_Token op;
    CCL_Token token;
} CCL_Expr;

typedef enum {
    CCL_STMT_NONE = 0,
    CCL_STMT_LET,
    CCL_STMT_SET,
    CCL_STMT_VOID,
    CCL_STMT_VOIDLET,
    CCL_STMT_SLET,
    CCL_STMT_SAY,
    CCL_STMT_RETURN,
    CCL_STMT_EXPR,
    CCL_STMT_BLOCK,
    CCL_STMT_IF,
    CCL_STMT_WHILE,
    CCL_STMT_FOR
} CCL_StmtType;

typedef struct {
    CCL_StmtType type;
    uint32_t expr;
    uint32_t aux;
    CCL_Token token;
} CCL_Stmt;

typedef struct {
    CCL_Expr exprs[256];
    CCL_Stmt stmts[256];
    uint32_t expr_count;
    uint32_t stmt_count;
} CCL_Program;

typedef struct {
    uint32_t tokens;
    uint32_t errors;
    char message[128];
} CCL_Result;

void ccl_lexer_init(CCL_Lexer* lexer, const char* source);
CCL_Token ccl_lexer_next(CCL_Lexer* lexer);
const char* ccl_token_name(CCL_TokenType type);

CCL_Result ccl_compile_source(const char* source, CCL_Program* program);

#ifdef __cplusplus
}
#endif
