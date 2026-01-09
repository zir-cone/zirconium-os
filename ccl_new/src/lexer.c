#include "core.h"

static bool is_alpha(char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static bool is_digit(char c){ return (c>='0'&&c<='9'); }
static bool is_alnum(char c){ return is_alpha(c)||is_digit(c); }
static char lowerc(char c){ if (c>='A'&&c<='Z') return (char)(c - 'A' + 'a'); return c; }

static void push_tok(Lexer* L, Token t) {
    if (L->ntoks+1 > L->cap) {
        L->cap = (L->cap==0)? 256 : L->cap*2;
        L->toks = (Token*)xrealloc(L->toks, L->cap*sizeof(Token));
    }
    L->toks[L->ntoks++] = t;
}

static bool match_kw(const Source* S, size_t start, size_t end, const char* kw) {
    size_t n = end - start;
    size_t k = strlen(kw);
    if (n != k) return false;
    for (size_t i=0;i<n;i++) if (lowerc(S->src[start+i]) != lowerc(kw[i])) return false;
    return true;
}

static TokenKind ident_to_kw(const Source* S, size_t start, size_t end) {
    if (match_kw(S,start,end,"define"))   return TK_DEFINE;
    if (match_kw(S,start,end,"function")) return TK_FUNCTION;
    if (match_kw(S,start,end,"class"))    return TK_CLASS;

    if (match_kw(S,start,end,"let"))      return TK_LET;
    if (match_kw(S,start,end,"set"))      return TK_SET;
    if (match_kw(S,start,end,"void"))     return TK_VOID;
    if (match_kw(S,start,end,"voidlet"))  return TK_VOIDLET;
    if (match_kw(S,start,end,"slet"))     return TK_SLET;

    if (match_kw(S,start,end,"return"))   return TK_RETURN;
    if (match_kw(S,start,end,"nothing"))  return TK_NOTHING;

    if (match_kw(S,start,end,"if"))       return TK_IF;
    if (match_kw(S,start,end,"else"))     return TK_ELSE;

    if (match_kw(S,start,end,"for"))      return TK_FOR;
    if (match_kw(S,start,end,"while"))    return TK_WHILE;
    if (match_kw(S,start,end,"within"))   return TK_WITHIN;

    if (match_kw(S,start,end,"say"))      return TK_SAY;

    if (match_kw(S,start,end,"and"))      return TK_AND;
    if (match_kw(S,start,end,"or"))       return TK_OR;
    if (match_kw(S,start,end,"not"))      return TK_NOT;
    if (match_kw(S,start,end,"nand"))     return TK_NAND;
    if (match_kw(S,start,end,"nor"))      return TK_NOR;

    if (match_kw(S,start,end,"null"))     return TK_NULL;
    if (match_kw(S,start,end,"true"))     return TK_TRUE;
    if (match_kw(S,start,end,"false"))    return TK_FALSE;

    if (match_kw(S,start,end,"public"))   return TK_PUBLIC;
    if (match_kw(S,start,end,"private"))  return TK_PRIVATE;

    return TK_IDENT;
}

void lex_all(Lexer* L) {
    const char* s = L->S.src;
    const size_t n = L->S.src_len;

    L->i = 0; L->line = 1; L->col = 1;
    bool had_gap_since_last = true;

    while (L->i < n) {
        char c = s[L->i];

        // whitespace
        if (c==' '||c=='\t'||c=='\r'||c=='\n') {
            had_gap_since_last = true;
            if (c=='\n'){ L->line++; L->col=1; L->i++; }
            else { L->col++; L->i++; }
            continue;
        }

        // comment ;;; to end of line
        if (c==';' && L->i+2<n && s[L->i+1]==';' && s[L->i+2]==';') {
            had_gap_since_last = true;
            while (L->i<n && s[L->i] != '\n') { L->i++; L->col++; }
            continue;
        }

        Token t;
        t.start = L->i;
        t.line = L->line;
        t.col  = L->col;
        t.adjacent_to_next = false;

        // decorator @ident
        if (c=='@') {
            L->i++; L->col++;
            if (L->i<n && is_alpha(s[L->i])) {
                while (L->i<n && is_alnum(s[L->i])) { L->i++; L->col++; }
            }
            t.kind = TK_AT_IDENT;
            t.end = L->i;
            push_tok(L,t);
            if (L->ntoks>=2) L->toks[L->ntoks-2].adjacent_to_next = !had_gap_since_last;
            had_gap_since_last = false;
            continue;
        }

        // ellipsis ...
        if (c=='.' && L->i+2<n && s[L->i+1]=='.' && s[L->i+2]=='.') {
            L->i += 3; L->col += 3;
            t.end = L->i;
            t.kind = TK_ELLIPSIS;
            push_tok(L,t);
            if (L->ntoks>=2) L->toks[L->ntoks-2].adjacent_to_next = !had_gap_since_last;
            had_gap_since_last = false;
            continue;
        }

        // identifiers / keywords
        if (is_alpha(c)) {
            L->i++; L->col++;
            while (L->i<n && is_alnum(s[L->i])) { L->i++; L->col++; }
            t.end = L->i;
            t.kind = ident_to_kw(&L->S, t.start, t.end);
            push_tok(L,t);
            if (L->ntoks>=2) L->toks[L->ntoks-2].adjacent_to_next = !had_gap_since_last;
            had_gap_since_last = false;
            continue;
        }

        // integer literal
        if (is_digit(c)) {
            L->i++; L->col++;
            while (L->i<n && is_digit(s[L->i])) { L->i++; L->col++; }
            t.end = L->i;
            t.kind = TK_INT_LIT;
            push_tok(L,t);
            if (L->ntoks>=2) L->toks[L->ntoks-2].adjacent_to_next = !had_gap_since_last;
            had_gap_since_last = false;
            continue;
        }

        // string literal "..." (no escapes yet)
        if (c=='"') {
            L->i++; L->col++;
            while (L->i<n && s[L->i]!='"') {
                if (s[L->i]=='\n') die_at(&L->S, L->line, L->col, "SyntaxError", "unterminated string literal");
                L->i++; L->col++;
            }
            if (L->i>=n) die_at(&L->S, L->line, L->col, "SyntaxError", "unterminated string literal");
            L->i++; L->col++;
            t.end = L->i;
            t.kind = TK_STR_LIT;
            push_tok(L,t);
            if (L->ntoks>=2) L->toks[L->ntoks-2].adjacent_to_next = !had_gap_since_last;
            had_gap_since_last = false;
            continue;
        }

        // multi-char operators
        if (c=='+' && L->i+1<n && s[L->i+1]=='+') { L->i+=2; L->col+=2; t.end=L->i; t.kind=TK_PLUSPLUS; push_tok(L,t); goto done_tok; }
        if (c=='-' && L->i+1<n && s[L->i+1]=='-') { L->i+=2; L->col+=2; t.end=L->i; t.kind=TK_MINUSMINUS; push_tok(L,t); goto done_tok; }

        if (c=='-' && L->i+1<n && s[L->i+1]=='>') { L->i+=2; L->col+=2; t.end=L->i; t.kind=TK_ARROW; push_tok(L,t); goto done_tok; }
        if (c=='>' && L->i+1<n && s[L->i+1]=='>') { L->i+=2; L->col+=2; t.end=L->i; t.kind=TK_RARROW; push_tok(L,t); goto done_tok; }

        if (c=='=' && L->i+1<n && s[L->i+1]=='=') { L->i+=2; L->col+=2; t.end=L->i; t.kind=TK_EQEQ; push_tok(L,t); goto done_tok; }
        if (c=='!' && L->i+1<n && s[L->i+1]=='=') { L->i+=2; L->col+=2; t.end=L->i; t.kind=TK_NEQ; push_tok(L,t); goto done_tok; }

        if (c=='<' && L->i+1<n && s[L->i+1]=='=') { L->i+=2; L->col+=2; t.end=L->i; t.kind=TK_LTE; push_tok(L,t); goto done_tok; }
        if (c=='>' && L->i+1<n && s[L->i+1]=='=') { L->i+=2; L->col+=2; t.end=L->i; t.kind=TK_GTE; push_tok(L,t); goto done_tok; }

        if (c=='+' && L->i+1<n && s[L->i+1]=='=') { L->i+=2; L->col+=2; t.end=L->i; t.kind=TK_PLUSEQ; push_tok(L,t);    goto done_tok; }
        if (c=='-' && L->i+1<n && s[L->i+1]=='=') { L->i+=2; L->col+=2; t.end=L->i; t.kind=TK_MINUSEQ; push_tok(L,t);   goto done_tok; }
        if (c=='*' && L->i+1<n && s[L->i+1]=='=') { L->i+=2; L->col+=2; t.end=L->i; t.kind=TK_STAREQ; push_tok(L,t);    goto done_tok; }
        if (c=='/' && L->i+1<n && s[L->i+1]=='=') { L->i+=2; L->col+=2; t.end=L->i; t.kind=TK_SLASHEQ; push_tok(L,t);   goto done_tok; }
        if (c=='%' && L->i+1<n && s[L->i+1]=='=') { L->i+=2; L->col+=2; t.end=L->i; t.kind=TK_PERCENTEQ; push_tok(L,t); goto done_tok; }


        // single-char tokens
        L->i++; L->col++;
        t.end = L->i;
        switch (c) {
            case '(': t.kind=TK_LPAREN;   break;
            case ')': t.kind=TK_RPAREN;   break;
            case '{': t.kind=TK_LBRACE;   break;
            case '}': t.kind=TK_RBRACE;   break;
            case '[': t.kind=TK_LBRACKET; break;
            case ']': t.kind=TK_RBRACKET; break;
            case ',': t.kind=TK_COMMA;    break;
            case ';': t.kind=TK_SEMI;     break;
            case '+': t.kind=TK_PLUS;     break;
            case '-': t.kind=TK_MINUS;    break;
            case '*': t.kind=TK_STAR;     break;
            case '/': t.kind=TK_SLASH;    break;
            case '%': t.kind=TK_PERCENT;  break;
            case '=': t.kind=TK_EQ;       break;
            case '<': t.kind=TK_LT;       break;
            case '>': t.kind=TK_GT;       break;
            default:
                die_at(&L->S, t.line, t.col, "SyntaxError", "invalid character");
        }
        push_tok(L,t);

done_tok:
        if (L->ntoks>=2) L->toks[L->ntoks-2].adjacent_to_next = !had_gap_since_last;
        had_gap_since_last = false;
    }

    Token eof;
    eof.kind=TK_EOF;
    eof.start=eof.end=L->i;
    eof.line=L->line;
    eof.col=L->col;
    eof.adjacent_to_next=false;
    push_tok(L,eof);
    if (L->ntoks>=2) L->toks[L->ntoks-2].adjacent_to_next = !had_gap_since_last;
}