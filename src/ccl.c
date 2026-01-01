#include "ccl.h"

static int ccl_is_alpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int ccl_is_digit(char c) {
    return (c >= '0' && c <= '9');
}

static CCL_Token make_token(CCL_TokenType type, const char* start, const char* end, uint32_t line) {
    CCL_Token token;
    token.type = type;
    token.start = start;
    token.length = (uint32_t)(end - start);
    token.line = line;
    return token;
}

void ccl_lexer_init(CCL_Lexer* lexer, const char* source) {
    if (!lexer) return;
    lexer->cursor = source ? source : "";
    lexer->line = 1;
}

CCL_Token ccl_lexer_next(CCL_Lexer* lexer) {
    if (!lexer) return make_token(CCL_TOKEN_EOF, "", "", 0);

    const char* c = lexer->cursor;
    while (*c == ' ' || *c == '\t' || *c == '\r') {
        ++c;
    }

    if (*c == '\n') {
        ++c;
        lexer->cursor = c;
        ++lexer->line;
        return make_token(CCL_TOKEN_NEWLINE, c - 1, c, lexer->line - 1);
    }

    if (*c == 0) {
        lexer->cursor = c;
        return make_token(CCL_TOKEN_EOF, c, c, lexer->line);
    }

    const char* start = c;

    if (ccl_is_alpha(*c)) {
        ++c;
        while (ccl_is_alpha(*c) || ccl_is_digit(*c)) {
            ++c;
        }
        lexer->cursor = c;
        return make_token(CCL_TOKEN_IDENTIFIER, start, c, lexer->line);
    }

    if (ccl_is_digit(*c)) {
        ++c;
        while (ccl_is_digit(*c)) {
            ++c;
        }
        lexer->cursor = c;
        return make_token(CCL_TOKEN_NUMBER, start, c, lexer->line);
    }

    if (*c == '"') {
        ++c;
        while (*c && *c != '"') {
            if (*c == '\n') {
                ++lexer->line;
            }
            ++c;
        }
        if (*c == '"') {
            ++c;
        }
        lexer->cursor = c;
        return make_token(CCL_TOKEN_STRING, start, c, lexer->line);
    }

    ++c;
    lexer->cursor = c;

    switch (*start) {
        case '(': return make_token(CCL_TOKEN_LPAREN, start, c, lexer->line);
        case ')': return make_token(CCL_TOKEN_RPAREN, start, c, lexer->line);
        case ',': return make_token(CCL_TOKEN_COMMA, start, c, lexer->line);
        case '=': return make_token(CCL_TOKEN_EQUAL, start, c, lexer->line);
        default:  return make_token(CCL_TOKEN_UNKNOWN, start, c, lexer->line);
    }
}

CCL_Result ccl_compile_source(const char* source) {
    CCL_Result result;
    result.tokens = 0;
    result.errors = 0;
    for (size_t i = 0; i < sizeof(result.message); ++i) {
        result.message[i] = 0;
    }

    CCL_Lexer lexer;
    ccl_lexer_init(&lexer, source);

    while (1) {
        CCL_Token token = ccl_lexer_next(&lexer);
        if (token.type == CCL_TOKEN_EOF) break;
        if (token.type == CCL_TOKEN_UNKNOWN) {
            if (result.errors == 0) {
                const char* msg = "Unexpected token in CCL source";
                size_t i = 0;
                while (msg[i] && i + 1 < sizeof(result.message)) {
                    result.message[i] = msg[i];
                    ++i;
                }
                result.message[i] = 0;
            }
            ++result.errors;
        }
        ++result.tokens;
    }

    return result;
}
