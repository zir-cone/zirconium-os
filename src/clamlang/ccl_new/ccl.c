#include "ccl.h"

static int ccl_is_alpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int ccl_is_digit(char c) {
    return (c >= '0' && c <= '9');
}

static int ccl_is_alnum(char c) {
    return ccl_is_alpha(c) || ccl_is_digit(c);
}

static CCL_Token make_token(CCL_TokenType type, const char* start, const char* end, uint32_t line) {
    CCL_Token token;
    token.type = type;
    token.start = start;
    token.length = (uint32_t)(end - start);
    token.line = line;
    return token;
}

static int token_eq(const CCL_Token* token, const char* text) {
    uint32_t i = 0;
    while (text[i] != 0) {
        if (i >= token->length) return 0;
        if (token->start[i] != text[i]) return 0;
        ++i;
    }
    return i == token->length;
}

static CCL_TokenType keyword_type(const CCL_Token* token) {
    if (token_eq(token, "LET")) return CCL_TOKEN_LET;
    if (token_eq(token, "SET")) return CCL_TOKEN_SET;
    if (token_eq(token, "VOID")) return CCL_TOKEN_VOID;
    if (token_eq(token, "VOIDLET")) return CCL_TOKEN_VOIDLET;
    if (token_eq(token, "SLET")) return CCL_TOKEN_SLET;
    if (token_eq(token, "IF")) return CCL_TOKEN_IF;
    if (token_eq(token, "ELSE")) return CCL_TOKEN_ELSE;
    if (token_eq(token, "WHILE")) return CCL_TOKEN_WHILE;
    if (token_eq(token, "FOR")) return CCL_TOKEN_FOR;
    if (token_eq(token, "WITHIN")) return CCL_TOKEN_WITHIN;
    if (token_eq(token, "DEFINE")) return CCL_TOKEN_DEFINE;
    if (token_eq(token, "FUNCTION")) return CCL_TOKEN_FUNCTION;
    if (token_eq(token, "RETURN")) return CCL_TOKEN_RETURN;
    if (token_eq(token, "IMPORT")) return CCL_TOKEN_IMPORT;
    if (token_eq(token, "CLASS")) return CCL_TOKEN_CLASS;
    if (token_eq(token, "PUBLIC")) return CCL_TOKEN_PUBLIC;
    if (token_eq(token, "PRIVATE")) return CCL_TOKEN_PRIVATE;
    if (token_eq(token, "INIT")) return CCL_TOKEN_INIT;
    if (token_eq(token, "TRUE")) return CCL_TOKEN_TRUE;
    if (token_eq(token, "FALSE")) return CCL_TOKEN_FALSE;
    if (token_eq(token, "Null")) return CCL_TOKEN_NULL;
    if (token_eq(token, "SAY")) return CCL_TOKEN_SAY;
    if (token_eq(token, "LENGTH")) return CCL_TOKEN_LENGTH;
    if (token_eq(token, "RANGE")) return CCL_TOKEN_RANGE;
    if (token_eq(token, "AND")) return CCL_TOKEN_AND;
    if (token_eq(token, "OR")) return CCL_TOKEN_OR;
    if (token_eq(token, "NOT")) return CCL_TOKEN_NOT;
    return CCL_TOKEN_IDENTIFIER;
}

void ccl_lexer_init(CCL_Lexer* lexer, const char* source) {
    if (!lexer) return;
    lexer->cursor = source ? source : "";
    lexer->line = 1;
}

static CCL_Token read_string(CCL_Lexer* lexer, CCL_TokenType type, const char* start) {
    const char* c = lexer->cursor;
    while (*c) {
        if (*c == '\n') {
            ++lexer->line;
        }
        if (type != CCL_TOKEN_STRING_RAW && *c == '\\' && c[1] != 0) {
            c += 2;
            continue;
        }
        if (*c == '"') {
            ++c;
            lexer->cursor = c;
            return make_token(type, start, c, lexer->line);
        }
        ++c;
    }
    lexer->cursor = c;
    return make_token(CCL_TOKEN_ERROR, start, c, lexer->line);
}

CCL_Token ccl_lexer_next(CCL_Lexer* lexer) {
    if (!lexer) return make_token(CCL_TOKEN_EOF, "", "", 0);

    const char* c = lexer->cursor;
    while (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n') {
        if (*c == '\n') {
            ++lexer->line;
        }
        ++c;
    }

    if (*c == ';' && c[1] == ';' && c[2] == ';') {
        c += 3;
        while (*c && *c != '\n') {
            ++c;
        }
        lexer->cursor = c;
        return ccl_lexer_next(lexer);
    }

    if (*c == 0) {
        lexer->cursor = c;
        return make_token(CCL_TOKEN_EOF, c, c, lexer->line);
    }

    const char* start = c;

    if (*c == '$' && c[1] == 'F' && c[2] == '"') {
        c += 3;
        lexer->cursor = c;
        return read_string(lexer, CCL_TOKEN_STRING_FMT, start);
    }

    if (*c == '$' && c[1] == '"') {
        c += 2;
        lexer->cursor = c;
        return read_string(lexer, CCL_TOKEN_STRING_ESC, start);
    }

    if (*c == '"') {
        ++c;
        lexer->cursor = c;
        return read_string(lexer, CCL_TOKEN_STRING_RAW, start);
    }

    if (ccl_is_alpha(*c)) {
        ++c;
        while (ccl_is_alnum(*c)) {
            ++c;
        }
        lexer->cursor = c;
        CCL_Token token = make_token(CCL_TOKEN_IDENTIFIER, start, c, lexer->line);
        token.type = keyword_type(&token);
        return token;
    }

    if (ccl_is_digit(*c)) {
        ++c;
        while (ccl_is_digit(*c)) {
            ++c;
        }
        if (*c == '.' && ccl_is_digit(c[1])) {
            ++c;
            while (ccl_is_digit(*c)) {
                ++c;
            }
        }
        lexer->cursor = c;
        return make_token(CCL_TOKEN_NUMBER, start, c, lexer->line);
    }

    ++c;
    lexer->cursor = c;

    switch (*start) {
        case ';': return make_token(CCL_TOKEN_SEMICOLON, start, c, lexer->line);
        case ',': return make_token(CCL_TOKEN_COMMA, start, c, lexer->line);
        case '(': return make_token(CCL_TOKEN_LPAREN, start, c, lexer->line);
        case ')': return make_token(CCL_TOKEN_RPAREN, start, c, lexer->line);
        case '{': return make_token(CCL_TOKEN_LBRACE, start, c, lexer->line);
        case '}': return make_token(CCL_TOKEN_RBRACE, start, c, lexer->line);
        case '[': return make_token(CCL_TOKEN_LBRACKET, start, c, lexer->line);
        case ']': return make_token(CCL_TOKEN_RBRACKET, start, c, lexer->line);
        case '+':
            if (*c == '+') { ++lexer->cursor; return make_token(CCL_TOKEN_PLUS_PLUS, start, c + 1, lexer->line); }
            if (*c == '=') { ++lexer->cursor; return make_token(CCL_TOKEN_PLUS_EQUAL, start, c + 1, lexer->line); }
            return make_token(CCL_TOKEN_PLUS, start, c, lexer->line);
        case '-':
            if (*c == '-') { ++lexer->cursor; return make_token(CCL_TOKEN_MINUS_MINUS, start, c + 1, lexer->line); }
            if (*c == '=') { ++lexer->cursor; return make_token(CCL_TOKEN_MINUS_EQUAL, start, c + 1, lexer->line); }
            if (*c == '>') { ++lexer->cursor; return make_token(CCL_TOKEN_ARROW, start, c + 1, lexer->line); }
            return make_token(CCL_TOKEN_MINUS, start, c, lexer->line);
        case '*':
            if (*c == '=') { ++lexer->cursor; return make_token(CCL_TOKEN_STAR_EQUAL, start, c + 1, lexer->line); }
            return make_token(CCL_TOKEN_STAR, start, c, lexer->line);
        case '/':
            if (*c == '=') { ++lexer->cursor; return make_token(CCL_TOKEN_SLASH_EQUAL, start, c + 1, lexer->line); }
            return make_token(CCL_TOKEN_SLASH, start, c, lexer->line);
        case '%': return make_token(CCL_TOKEN_PERCENT, start, c, lexer->line);
        case '=':
            if (*c == '=') { ++lexer->cursor; return make_token(CCL_TOKEN_EQUAL_EQUAL, start, c + 1, lexer->line); }
            return make_token(CCL_TOKEN_EQUAL, start, c, lexer->line);
        case '!':
            if (*c == '=') { ++lexer->cursor; return make_token(CCL_TOKEN_BANG_EQUAL, start, c + 1, lexer->line); }
            return make_token(CCL_TOKEN_BANG, start, c, lexer->line);
        case '<':
            if (*c == '=') { ++lexer->cursor; return make_token(CCL_TOKEN_LT_EQUAL, start, c + 1, lexer->line); }
            return make_token(CCL_TOKEN_LT, start, c, lexer->line);
        case '>':
            if (*c == '=') { ++lexer->cursor; return make_token(CCL_TOKEN_GT_EQUAL, start, c + 1, lexer->line); }
            if (*c == '>') { ++lexer->cursor; return make_token(CCL_TOKEN_SHIFT_RIGHT, start, c + 1, lexer->line); }
            return make_token(CCL_TOKEN_GT, start, c, lexer->line);
        default:
            return make_token(CCL_TOKEN_ERROR, start, c, lexer->line);
    }
}

const char* ccl_token_name(CCL_TokenType type) {
    switch (type) {
        case CCL_TOKEN_EOF: return "EOF";
        case CCL_TOKEN_ERROR: return "ERROR";
        case CCL_TOKEN_IDENTIFIER: return "IDENTIFIER";
        case CCL_TOKEN_NUMBER: return "NUMBER";
        case CCL_TOKEN_STRING_RAW: return "STRING_RAW";
        case CCL_TOKEN_STRING_ESC: return "STRING_ESC";
        case CCL_TOKEN_STRING_FMT: return "STRING_FMT";
        case CCL_TOKEN_SEMICOLON: return "SEMICOLON";
        case CCL_TOKEN_COMMA: return "COMMA";
        case CCL_TOKEN_LPAREN: return "LPAREN";
        case CCL_TOKEN_RPAREN: return "RPAREN";
        case CCL_TOKEN_LBRACE: return "LBRACE";
        case CCL_TOKEN_RBRACE: return "RBRACE";
        case CCL_TOKEN_LBRACKET: return "LBRACKET";
        case CCL_TOKEN_RBRACKET: return "RBRACKET";
        case CCL_TOKEN_PLUS: return "PLUS";
        case CCL_TOKEN_MINUS: return "MINUS";
        case CCL_TOKEN_STAR: return "STAR";
        case CCL_TOKEN_SLASH: return "SLASH";
        case CCL_TOKEN_PERCENT: return "PERCENT";
        case CCL_TOKEN_EQUAL: return "EQUAL";
        case CCL_TOKEN_EQUAL_EQUAL: return "EQUAL_EQUAL";
        case CCL_TOKEN_BANG: return "BANG";
        case CCL_TOKEN_BANG_EQUAL: return "BANG_EQUAL";
        case CCL_TOKEN_LT: return "LT";
        case CCL_TOKEN_LT_EQUAL: return "LT_EQUAL";
        case CCL_TOKEN_GT: return "GT";
        case CCL_TOKEN_GT_EQUAL: return "GT_EQUAL";
        case CCL_TOKEN_PLUS_EQUAL: return "PLUS_EQUAL";
        case CCL_TOKEN_MINUS_EQUAL: return "MINUS_EQUAL";
        case CCL_TOKEN_STAR_EQUAL: return "STAR_EQUAL";
        case CCL_TOKEN_SLASH_EQUAL: return "SLASH_EQUAL";
        case CCL_TOKEN_ARROW: return "ARROW";
        case CCL_TOKEN_SHIFT_RIGHT: return "SHIFT_RIGHT";
        case CCL_TOKEN_PLUS_PLUS: return "PLUS_PLUS";
        case CCL_TOKEN_MINUS_MINUS: return "MINUS_MINUS";
        case CCL_TOKEN_AND: return "AND";
        case CCL_TOKEN_OR: return "OR";
        case CCL_TOKEN_NOT: return "NOT";
        case CCL_TOKEN_LET: return "LET";
        case CCL_TOKEN_SET: return "SET";
        case CCL_TOKEN_VOID: return "VOID";
        case CCL_TOKEN_VOIDLET: return "VOIDLET";
        case CCL_TOKEN_SLET: return "SLET";
        case CCL_TOKEN_IF: return "IF";
        case CCL_TOKEN_ELSE: return "ELSE";
        case CCL_TOKEN_WHILE: return "WHILE";
        case CCL_TOKEN_FOR: return "FOR";
        case CCL_TOKEN_WITHIN: return "WITHIN";
        case CCL_TOKEN_DEFINE: return "DEFINE";
        case CCL_TOKEN_FUNCTION: return "FUNCTION";
        case CCL_TOKEN_RETURN: return "RETURN";
        case CCL_TOKEN_IMPORT: return "IMPORT";
        case CCL_TOKEN_CLASS: return "CLASS";
        case CCL_TOKEN_PUBLIC: return "PUBLIC";
        case CCL_TOKEN_PRIVATE: return "PRIVATE";
        case CCL_TOKEN_INIT: return "INIT";
        case CCL_TOKEN_TRUE: return "TRUE";
        case CCL_TOKEN_FALSE: return "FALSE";
        case CCL_TOKEN_NULL: return "NULL";
        case CCL_TOKEN_SAY: return "SAY";
        case CCL_TOKEN_LENGTH: return "LENGTH";
        case CCL_TOKEN_RANGE: return "RANGE";
        default: return "UNKNOWN";
    }
}

typedef struct {
    CCL_Lexer lexer;
    CCL_Token current;
    CCL_Token previous;
    CCL_Result* result;
    CCL_Program* program;
} CCL_Parser;

static void parser_advance(CCL_Parser* parser) {
    parser->previous = parser->current;
    parser->current = ccl_lexer_next(&parser->lexer);
    if (parser->current.type == CCL_TOKEN_ERROR) {
        if (parser->result->errors == 0) {
            const char* msg = "Lexer error";
            size_t i = 0;
            while (msg[i] && i + 1 < sizeof(parser->result->message)) {
                parser->result->message[i] = msg[i];
                ++i;
            }
            parser->result->message[i] = 0;
        }
        ++parser->result->errors;
    }
    ++parser->result->tokens;
}

static int parser_match(CCL_Parser* parser, CCL_TokenType type) {
    if (parser->current.type != type) return 0;
    parser_advance(parser);
    return 1;
}

static void parser_error(CCL_Parser* parser, const char* message) {
    if (parser->result->errors == 0) {
        size_t i = 0;
        while (message[i] && i + 1 < sizeof(parser->result->message)) {
            parser->result->message[i] = message[i];
            ++i;
        }
        parser->result->message[i] = 0;
    }
    ++parser->result->errors;
}

static void parser_sync(CCL_Parser* parser) {
    while (parser->current.type != CCL_TOKEN_EOF) {
        if (parser->previous.type == CCL_TOKEN_SEMICOLON) return;
        if (parser->current.type == CCL_TOKEN_LBRACE || parser->current.type == CCL_TOKEN_RBRACE) return;
        parser_advance(parser);
    }
}

static uint32_t add_expr(CCL_Parser* parser, CCL_Expr expr) {
    if (parser->program->expr_count >= (uint32_t)(sizeof(parser->program->exprs) / sizeof(parser->program->exprs[0]))) {
        parser_error(parser, "Expression limit reached");
        return 0;
    }
    uint32_t idx = parser->program->expr_count++;
    parser->program->exprs[idx] = expr;
    return idx;
}

static uint32_t add_stmt(CCL_Parser* parser, CCL_Stmt stmt) {
    if (parser->program->stmt_count >= (uint32_t)(sizeof(parser->program->stmts) / sizeof(parser->program->stmts[0]))) {
        parser_error(parser, "Statement limit reached");
        return 0;
    }
    uint32_t idx = parser->program->stmt_count++;
    parser->program->stmts[idx] = stmt;
    return idx;
}

static int precedence(CCL_TokenType type) {
    switch (type) {
        case CCL_TOKEN_OR: return 1;
        case CCL_TOKEN_AND: return 2;
        case CCL_TOKEN_EQUAL_EQUAL:
        case CCL_TOKEN_BANG_EQUAL: return 3;
        case CCL_TOKEN_LT:
        case CCL_TOKEN_LT_EQUAL:
        case CCL_TOKEN_GT:
        case CCL_TOKEN_GT_EQUAL: return 4;
        case CCL_TOKEN_PLUS:
        case CCL_TOKEN_MINUS: return 5;
        case CCL_TOKEN_STAR:
        case CCL_TOKEN_SLASH:
        case CCL_TOKEN_PERCENT: return 6;
        default: return 0;
    }
}

static uint32_t parse_expression(CCL_Parser* parser, int min_prec);

static uint32_t parse_primary(CCL_Parser* parser) {
    CCL_Token token = parser->current;
    if (parser_match(parser, CCL_TOKEN_IDENTIFIER) ||
        parser_match(parser, CCL_TOKEN_NUMBER) ||
        parser_match(parser, CCL_TOKEN_STRING_RAW) ||
        parser_match(parser, CCL_TOKEN_STRING_ESC) ||
        parser_match(parser, CCL_TOKEN_STRING_FMT) ||
        parser_match(parser, CCL_TOKEN_TRUE) ||
        parser_match(parser, CCL_TOKEN_FALSE) ||
        parser_match(parser, CCL_TOKEN_NULL)) {
        CCL_Expr expr;
        expr.type = CCL_EXPR_LITERAL;
        expr.left = 0;
        expr.right = 0;
        expr.op = token;
        expr.token = token;
        return add_expr(parser, expr);
    }

    if (parser_match(parser, CCL_TOKEN_LPAREN)) {
        uint32_t expr_idx = parse_expression(parser, 1);
        if (!parser_match(parser, CCL_TOKEN_RPAREN)) {
            parser_error(parser, "Expected ')' after expression");
        }
        return expr_idx;
    }

    parser_error(parser, "Expected expression");
    return 0;
}

static uint32_t parse_postfix(CCL_Parser* parser) {
    uint32_t expr_idx = parse_primary(parser);

    while (1) {
        if (parser_match(parser, CCL_TOKEN_LPAREN)) {
            uint32_t arg_count = 0;
            if (parser->current.type != CCL_TOKEN_RPAREN) {
                do {
                    parse_expression(parser, 1);
                    ++arg_count;
                } while (parser_match(parser, CCL_TOKEN_COMMA));
            }
            if (!parser_match(parser, CCL_TOKEN_RPAREN)) {
                parser_error(parser, "Expected ')' after call");
            }
            CCL_Expr call_expr;
            call_expr.type = CCL_EXPR_CALL;
            call_expr.left = expr_idx;
            call_expr.right = arg_count;
            call_expr.op = parser->previous;
            call_expr.token = parser->previous;
            expr_idx = add_expr(parser, call_expr);
            continue;
        }

        if (parser_match(parser, CCL_TOKEN_LBRACKET)) {
            uint32_t index_expr = parse_expression(parser, 1);
            if (!parser_match(parser, CCL_TOKEN_RBRACKET)) {
                parser_error(parser, "Expected ']' after index");
            }
            CCL_Expr idx_expr;
            idx_expr.type = CCL_EXPR_INDEX;
            idx_expr.left = expr_idx;
            idx_expr.right = index_expr;
            idx_expr.op = parser->previous;
            idx_expr.token = parser->previous;
            expr_idx = add_expr(parser, idx_expr);
            continue;
        }

        break;
    }

    return expr_idx;
}

static uint32_t parse_expression(CCL_Parser* parser, int min_prec) {
    uint32_t left = parse_postfix(parser);

    while (1) {
        int prec = precedence(parser->current.type);
        if (prec < min_prec) break;

        CCL_Token op = parser->current;
        parser_advance(parser);

        uint32_t right = parse_expression(parser, prec + 1);
        CCL_Expr expr;
        expr.type = CCL_EXPR_BINARY;
        expr.left = left;
        expr.right = right;
        expr.op = op;
        expr.token = op;
        left = add_expr(parser, expr);
    }

    return left;
}

static void parse_optional_type(CCL_Parser* parser) {
    if (parser_match(parser, CCL_TOKEN_ARROW)) {
        if (!parser_match(parser, CCL_TOKEN_IDENTIFIER)) {
            parser_error(parser, "Expected type name after ->");
        }
    }
}

static void parse_statement(CCL_Parser* parser) {
    CCL_Stmt stmt;
    stmt.type = CCL_STMT_NONE;
    stmt.expr = 0;
    stmt.aux = 0;
    stmt.token = parser->current;

    if (parser_match(parser, CCL_TOKEN_LBRACE)) {
        stmt.type = CCL_STMT_BLOCK;
        add_stmt(parser, stmt);
        while (parser->current.type != CCL_TOKEN_RBRACE && parser->current.type != CCL_TOKEN_EOF) {
            parse_statement(parser);
        }
        if (!parser_match(parser, CCL_TOKEN_RBRACE)) {
            parser_error(parser, "Expected '}' after block");
        }
        return;
    }

    if (parser_match(parser, CCL_TOKEN_LET) || parser_match(parser, CCL_TOKEN_VOIDLET) || parser_match(parser, CCL_TOKEN_SLET)) {
        stmt.token = parser->previous;
        stmt.type = (stmt.token.type == CCL_TOKEN_LET) ? CCL_STMT_LET :
                    (stmt.token.type == CCL_TOKEN_VOIDLET) ? CCL_STMT_VOIDLET : CCL_STMT_SLET;
        if (!parser_match(parser, CCL_TOKEN_IDENTIFIER)) {
            parser_error(parser, "Expected identifier after LET/VOIDLET/SLET");
            parser_sync(parser);
            return;
        }
        parse_optional_type(parser);
        if (parser_match(parser, CCL_TOKEN_EQUAL)) {
            stmt.expr = parse_expression(parser, 1);
        }
        if (!parser_match(parser, CCL_TOKEN_SEMICOLON)) {
            parser_error(parser, "Expected ';' after declaration");
        }
        add_stmt(parser, stmt);
        return;
    }

    if (parser_match(parser, CCL_TOKEN_SET)) {
        stmt.type = CCL_STMT_SET;
        if (!parser_match(parser, CCL_TOKEN_IDENTIFIER)) {
            parser_error(parser, "Expected identifier after SET");
            parser_sync(parser);
            return;
        }
        if (parser_match(parser, CCL_TOKEN_EQUAL) ||
            parser_match(parser, CCL_TOKEN_PLUS_EQUAL) ||
            parser_match(parser, CCL_TOKEN_MINUS_EQUAL) ||
            parser_match(parser, CCL_TOKEN_STAR_EQUAL) ||
            parser_match(parser, CCL_TOKEN_SLASH_EQUAL)) {
            stmt.expr = parse_expression(parser, 1);
        } else {
            parser_error(parser, "Expected assignment after SET identifier");
        }
        if (!parser_match(parser, CCL_TOKEN_SEMICOLON)) {
            parser_error(parser, "Expected ';' after SET");
        }
        add_stmt(parser, stmt);
        return;
    }

    if (parser_match(parser, CCL_TOKEN_VOID)) {
        stmt.type = CCL_STMT_VOID;
        if (!parser_match(parser, CCL_TOKEN_IDENTIFIER)) {
            parser_error(parser, "Expected identifier after VOID");
            parser_sync(parser);
            return;
        }
        if (!parser_match(parser, CCL_TOKEN_SEMICOLON)) {
            parser_error(parser, "Expected ';' after VOID");
        }
        add_stmt(parser, stmt);
        return;
    }

    if (parser_match(parser, CCL_TOKEN_SAY)) {
        stmt.type = CCL_STMT_SAY;
        stmt.expr = parse_expression(parser, 1);
        if (!parser_match(parser, CCL_TOKEN_SEMICOLON)) {
            parser_error(parser, "Expected ';' after SAY");
        }
        add_stmt(parser, stmt);
        return;
    }

    if (parser_match(parser, CCL_TOKEN_RETURN)) {
        stmt.type = CCL_STMT_RETURN;
        if (parser->current.type != CCL_TOKEN_SEMICOLON) {
            stmt.expr = parse_expression(parser, 1);
        }
        if (!parser_match(parser, CCL_TOKEN_SEMICOLON)) {
            parser_error(parser, "Expected ';' after RETURN");
        }
        add_stmt(parser, stmt);
        return;
    }

    if (parser_match(parser, CCL_TOKEN_IF)) {
        stmt.type = CCL_STMT_IF;
        if (!parser_match(parser, CCL_TOKEN_LPAREN)) {
            parser_error(parser, "Expected '(' after IF");
        }
        stmt.expr = parse_expression(parser, 1);
        if (!parser_match(parser, CCL_TOKEN_RPAREN)) {
            parser_error(parser, "Expected ')' after IF condition");
        }
        parse_statement(parser);
        if (parser_match(parser, CCL_TOKEN_ELSE)) {
            parse_statement(parser);
        }
        add_stmt(parser, stmt);
        return;
    }

    if (parser_match(parser, CCL_TOKEN_WHILE)) {
        stmt.type = CCL_STMT_WHILE;
        if (!parser_match(parser, CCL_TOKEN_LPAREN)) {
            parser_error(parser, "Expected '(' after WHILE");
        }
        stmt.expr = parse_expression(parser, 1);
        if (!parser_match(parser, CCL_TOKEN_RPAREN)) {
            parser_error(parser, "Expected ')' after WHILE condition");
        }
        parse_statement(parser);
        add_stmt(parser, stmt);
        return;
    }

    if (parser_match(parser, CCL_TOKEN_FOR)) {
        stmt.type = CCL_STMT_FOR;
        if (!parser_match(parser, CCL_TOKEN_LPAREN)) {
            parser_error(parser, "Expected '(' after FOR");
        }
        while (parser->current.type != CCL_TOKEN_RPAREN && parser->current.type != CCL_TOKEN_EOF) {
            parser_advance(parser);
        }
        if (!parser_match(parser, CCL_TOKEN_RPAREN)) {
            parser_error(parser, "Expected ')' after FOR clause");
        }
        parse_statement(parser);
        add_stmt(parser, stmt);
        return;
    }

    stmt.type = CCL_STMT_EXPR;
    stmt.expr = parse_expression(parser, 1);
    if (!parser_match(parser, CCL_TOKEN_SEMICOLON)) {
        parser_error(parser, "Expected ';' after expression");
    }
    add_stmt(parser, stmt);
}

CCL_Result ccl_compile_source(const char* source, CCL_Program* program) {
    CCL_Result result;
    result.tokens = 0;
    result.errors = 0;
    for (size_t i = 0; i < sizeof(result.message); ++i) {
        result.message[i] = 0;
    }

    if (program) {
        program->expr_count = 0;
        program->stmt_count = 0;
    }

    CCL_Parser parser;
    ccl_lexer_init(&parser.lexer, source);
    parser.current = ccl_lexer_next(&parser.lexer);
    parser.previous = parser.current;
    parser.result = &result;
    parser.program = program;

    while (parser.current.type != CCL_TOKEN_EOF) {
        parse_statement(&parser);
        if (result.errors > 0) {
            parser_sync(&parser);
        }
    }

    return result;
}
