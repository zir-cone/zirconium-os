#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CCL_TOKEN_EOF = 0,
    CCL_TOKEN_IDENTIFER,
    CCL_TOKEN_NUMBER,
    CCL_TOKEN_STRING,
    CCL_TOKEN_LPAREN,
    CCL_TOKEN_RPAREN,
    CCL_TOKEN_COMMA,
    CCL_TOKEN_EQUAL,
    CCL_TOKEN_NEWLINE,
    CCL_TOKEN_UNKNOWN
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

typedef struct {
    uint32_t tokens;
    uint32_t errors;
    char message[128];
} CCL_Result;

void ccl_lexer_init(CCL_Lexer* lexer, const char* source);
CCL_Token ccl_lexer_next(CCL_Lexer* lexer);
CCL_Result ccl_compile_source(const char* source);

#ifdef __cplusplus
}
#endif