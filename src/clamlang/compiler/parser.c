#include "core.h"

static Token* peek(Parser* P){ return &P->toks[P->pos]; }
static Token* prev(Parser* P){ return &P->toks[P->pos-1]; }
static bool at(Parser* P, TokenKind k){ return peek(P)->kind == k; }

static Token* consume(Parser* P, TokenKind k, const char* msg) {
    if (!at(P,k)) die_at(&P->S, peek(P)->line, peek(P)->col, "SyntaxError", msg);
    return &P->toks[P->pos++];
}

static bool match(Parser* P, TokenKind k) {
    if (at(P,k)) { P->pos++; return true; }
    return false;
}

static char* tok_text(const Parser* P, const Token* t) {
    size_t n = t->end - t->start;
    char* s = (char*)xmalloc(n+1);
    memcpy(s, P->S.src + t->start, n);
    s[n]=0;
    return s;
}

static char* tok_string_contents(const Parser* P, const Token* t) {
    size_t n = t->end - t->start;
    if (n < 2) return xstrdup("");
    const char* base = P->S.src + t->start + 1;
    size_t inner = n - 2;
    char* s = (char*)xmalloc(inner+1);
    memcpy(s, base, inner);
    s[inner]=0;
    return s;
}

static TypeTag type_from_name(const char* name) {
    if (_stricmp(name,"int")==0)  return TY_INT;
    if (_stricmp(name,"bool")==0) return TY_BOOL;
    if (_stricmp(name,"str")==0)  return TY_STR;
    if (_stricmp(name,"null")==0) return TY_NULL;
    if (_stricmp(name,"any")==0)  return TY_ANY;
    if (_stricmp(name,"list")==0) return TY_LIST;
    return TY_NONE;
}

static TypeSpec parse_typespec_opt(Parser* P) {
    TypeSpec ts = {0};
    ts.is_typed = false;
    ts.is_const_binding = false;
    ts.tag = TY_NONE;

    if (!match(P, TK_ARROW)) return ts;

    ts.is_typed = true;

    // optional "const" modifier as an identifier token (phase 1 legacy)
    if (at(P, TK_IDENT)) {
        Token* t0 = peek(P);
        char* s0 = tok_text(P, t0);
        if (_stricmp(s0,"const")==0) { P->pos++; ts.is_const_binding = true; }
        free(s0);
    }

    Token* t = consume(P, TK_IDENT, "expected type name after '->'");
    char* name = tok_text(P, t);
    ts.tag = type_from_name(name);
    free(name);

    if (ts.tag == TY_NONE) {
        die_at(&P->S, t->line, t->col, "TypeError", "unknown type name (phase 2 supports int/bool/str/list/Null/ANY)");
    }
    return ts;
}

/* =============== Expressions =============== */

static Expr* new_expr(ExprKind k, int line, int col) {
    Expr* e = (Expr*)xmalloc(sizeof(Expr));
    memset(e,0,sizeof(*e));
    e->kind=k; e->line=line; e->col=col;
    return e;
}

static Expr* parse_expr(Parser* P);

static Expr* parse_primary(Parser* P) {
    Token* t = peek(P);

    if (match(P, TK_INT_LIT)) {
        Expr* e = new_expr(EX_INT, t->line, t->col);
        char* s = tok_text(P, t);
        e->as.ival = _strtoi64(s, NULL, 10);
        free(s);
        return e;
    }

    if (match(P, TK_TRUE)) {
        Expr* e = new_expr(EX_BOOL, t->line, t->col);
        e->as.bval = true;
        return e;
    }

    if (match(P, TK_FALSE)) {
        Expr* e = new_expr(EX_BOOL, t->line, t->col);
        e->as.bval = false;
        return e;
    }

    if (match(P, TK_NULL)) {
        Expr* e = new_expr(EX_NULL, t->line, t->col);
        return e;
    }

    if (match(P, TK_STR_LIT)) {
        Expr* e = new_expr(EX_STR, t->line, t->col);
        e->as.sval = tok_string_contents(P, t);
        return e;
    }

    if (match(P, TK_ELLIPSIS)) {
        // ... is treated like an identifier (only valid inside varargs function)
        Expr* v = new_expr(EX_VAR, t->line, t->col);
        v->as.var.name = xstrdup("...");
        return v;
    }

    if (match(P, TK_LBRACKET)) {
        Expr* e = new_expr(EX_LIST, t->line, t->col);
        e->as.list.items = NULL;
        e->as.list.count = 0;

        if (!at(P, TK_RBRACKET)) {
            for (;;) {
                Expr* item = parse_expr(P);
                e->as.list.items = (Expr**)xrealloc(e->as.list.items, (e->as.list.count + 1) * sizeof(Expr*));
		e->as.list.items[e->as.list.count++] = item;

                if (match(P, TK_COMMA)) {
                    if (at(P, TK_RBRACKET)) break; // allow trailing comma
                    continue;
                }
                break;
            }
        }

        consume(P, TK_RBRACKET, "expected ']'");
        return e;
    }

    if (match(P, TK_IDENT)) {
        Token* nameTok = t;
        char* name = tok_text(P, nameTok);

        if (match(P, TK_LPAREN)) {
            Expr* call = new_expr(EX_CALL, nameTok->line, nameTok->col);
            call->as.call.callee = name;
            call->as.call.args = NULL;
            call->as.call.argc = 0;

            if (!at(P, TK_RPAREN)) {
                for (;;) {
                    Expr* a = parse_expr(P);
                    call->as.call.args = (Expr**)xrealloc(call->as.call.args, (call->as.call.argc+1)*sizeof(Expr*));
                    call->as.call.args[call->as.call.argc++] = a;
                    if (match(P, TK_COMMA)) {
                        if (at(P, TK_RPAREN)) break; // trailing comma
                        continue;
                    }
                    break;
                }
            }
            consume(P, TK_RPAREN, "expected ')'");
            return call;
        }

        Expr* v = new_expr(EX_VAR, nameTok->line, nameTok->col);
        v->as.var.name = name;
        return v;
    }

    if (match(P, TK_LPAREN)) {
        Expr* inner = parse_expr(P);
        consume(P, TK_RPAREN, "expected ')'");
        return inner;
    }

    die_at(&P->S, t->line, t->col, "SyntaxError", "expected expression");
    return NULL;
}

static Expr* parse_postfix(Parser* P) {
    Expr* e = parse_primary(P);
    for (;;) {
        if (match(P, TK_LBRACKET)) {
            Expr* idx = parse_expr(P);
            consume(P, TK_RBRACKET, "expected ']'");
            Expr* ix = new_expr(EX_INDEX, e->line, e->col);
            ix->as.index.base = e;
            ix->as.index.index = idx;
            e = ix;
            continue;
        }
        break;
    }
    return e;
}

static Expr* parse_unary(Parser* P) {
    Token* t = peek(P);
    if (match(P, TK_NOT)) {
        Expr* inner = parse_unary(P);
        Expr* u = new_expr(EX_UNARY, t->line, t->col);
        u->as.unary.op = (int)TK_NOT;
        u->as.unary.inner = inner;
        return u;
    }
    return parse_postfix(P);
}

static int precedence(TokenKind k) {
    switch (k) {
        case TK_STAR:
        case TK_SLASH:
	case TK_PERCENT: return 40;

        case TK_PLUS:
        case TK_MINUS: return 30;

        case TK_LT:
        case TK_LTE:
        case TK_GT:
        case TK_GTE:   return 20;

        case TK_EQEQ:
        case TK_NEQ:   return 15;

        case TK_AND:
        case TK_NAND:  return 10;

        case TK_OR:
        case TK_NOR:   return 5;

        default: return 0;
    }
}

static Expr* parse_bin_rhs(Parser* P, int min_prec, Expr* lhs) {
    for (;;) {
        TokenKind op = peek(P)->kind;
        int prec = precedence(op);
        if (prec < min_prec) return lhs;

        Token* optok = peek(P);
        P->pos++; // consume op

        Expr* rhs = parse_unary(P);
        TokenKind next = peek(P)->kind;
        int next_prec = precedence(next);
        if (next_prec > prec) rhs = parse_bin_rhs(P, prec+1, rhs);

        Expr* bin = new_expr(EX_BIN, optok->line, optok->col);
        bin->as.bin.op = (int)op;
        bin->as.bin.a = lhs;
        bin->as.bin.b = rhs;
        lhs = bin;
    }
}

static Expr* parse_expr(Parser* P) {
    Expr* lhs = parse_unary(P);
    return parse_bin_rhs(P, 1, lhs);
}

/* =============== Statements =============== */

static Stmt* new_stmt(StmtKind k, int line, int col) {
    Stmt* s = (Stmt*)xmalloc(sizeof(Stmt));
    memset(s,0,sizeof(*s));
    s->kind=k; s->line=line; s->col=col;
    return s;
}

static Stmt* parse_block(Parser* P);

static Stmt* parse_if(Parser* P) {
    Token* ifTok = prev(P); // IF already consumed

    consume(P, TK_LPAREN, "expected '(' after IF");
    Expr* cond = parse_expr(P);
    consume(P, TK_RPAREN, "expected ')' after IF condition");

    Stmt* then_block = parse_block(P);

    Stmt* node = new_stmt(ST_IF, ifTok->line, ifTok->col);
    node->as.if_stmt.cond = cond;
    node->as.if_stmt.then_block = then_block;
    node->as.if_stmt.else_branch = NULL;
    node->as.if_stmt.else_is_if = false;

    if (match(P, TK_ELSE)) {
        if (match(P, TK_IF)) {
            node->as.if_stmt.else_is_if = true;
            node->as.if_stmt.else_branch = parse_if(P);
        } else {
            node->as.if_stmt.else_is_if = false;
            node->as.if_stmt.else_branch = parse_block(P);
        }
    }

    return node;
}

static Stmt* parse_while(Parser* P) {
    Token* wTok = prev(P);
    consume(P, TK_LPAREN, "expected '(' after WHILE");
    Expr* cond = parse_expr(P);
    consume(P, TK_RPAREN, "expected ')' after WHILE condition");
    Stmt* body = parse_block(P);

    Stmt* st = new_stmt(ST_WHILE, wTok->line, wTok->col);
    st->as.while_stmt.cond = cond;
    st->as.while_stmt.body = body;
    return st;
}

static Stmt* parse_loop_header_stmt(Parser* P) {
    Token* t = peek(P);

    if (match(P, TK_LET) || match(P, TK_VOIDLET) || match(P, TK_SLET)) {
        TokenKind kw = prev(P)->kind;

        Token* nameTok = consume(P, TK_IDENT, "expected identifier in loop init/step");
        char* name = tok_text(P, nameTok);

        TypeSpec ts = parse_typespec_opt(P);

        consume(P, TK_EQ, "expected '='");
        Expr* rhs = parse_expr(P);

        Stmt* st = new_stmt(
            (kw==TK_LET)?ST_LET : (kw==TK_VOIDLET)?ST_VOIDLET : ST_SLET,
            t->line, t->col
        );
        st->as.let_stmt.name = name;
        st->as.let_stmt.ts = ts;
        st->as.let_stmt.rhs = rhs;
        return st;
    }

    if (match(P, TK_SET)) {
	Target tgt = parse_target(P);

	// no typepointer rewrite on SET
	if (at(P, TK_ARROW)) {
		die_at(&P->S, peek(P)->line, peek(P)->col, "TypepointerError", "Attempted to rewrite typepointer");
	}

	AssignOp op = parse_assign_op(P);
	Expr* rhs = parse_expr(P);

	Stmt* st = new_stmt(ST_SET, t->line, t->col);
	st->as.set_stmt.target = tgt;
	st->as.set_stmt.op     = op;
	st->as.set_stmt.rhs    = rhs;
	return st;
    }

    // i++ / i--
    if (at(P, TK_IDENT)) {
	// parse target: name or name[index]
	Target tgt = parse_target(P);

	if (match(P, TK_PLUSPLUS) || match(P, TK_MINUSMINUS)) {
		TokenKind op = prev(P)->kind;

		Stmt* st = new_stmt(ST_INCDEC, tgt.line, tgt.col);
		st->as.incdec_stmt.target = tgt;
		st->as.incdec_stmt.is_inc = (op == TK_PLUSPLUS);
		return st;
	}
	die_at(&P->S, peek(P)->line, peek(P)->col, "SyntaxError", "invalid FOR step");
    }

    die_at(&P->S, t->line, t->col, "SyntaxError", "invalid FOR init/step");
    return NULL;
}

static Stmt* parse_for(Parser* P) {
    Token* fTok = prev(P);

    consume(P, TK_LPAREN, "expected '(' after FOR");

    // init may be empty
    Stmt* init = NULL;
    char* iter_name = NULL;

    if (!at(P, TK_SEMI)) {
        init = parse_loop_header_stmt(P);
        // deduce iter var name from init
        if (init->kind == ST_LET || init->kind == ST_VOIDLET || init->kind == ST_SLET) iter_name = xstrdup(init->as.let_stmt.name);
        else if (init->kind == ST_SET) iter_name = xstrdup(init->as.set_stmt.name);
    }
    consume(P, TK_SEMI, "expected ';' after FOR init");

    consume(P, TK_WITHIN, "expected WITHIN in FOR header");
    Expr* limit = parse_expr(P);
    consume(P, TK_SEMI, "expected ';' after WITHIN <expr>");

    // step may be empty
    Stmt* step = NULL;
    if (!at(P, TK_RPAREN)) {
        step = parse_loop_header_stmt(P);
    }
    consume(P, TK_RPAREN, "expected ')' after FOR header");

    if (!iter_name) {
        die_at(&P->S, fTok->line, fTok->col, "SyntaxError", "FOR requires an init that establishes an iterator variable");
    }

    Stmt* body = parse_block(P);

    Stmt* st = new_stmt(ST_FOR, fTok->line, fTok->col);
    st->as.for_stmt.init = init;
    st->as.for_stmt.iter_name = iter_name;
    st->as.for_stmt.limit = limit;
    st->as.for_stmt.step = step;
    st->as.for_stmt.body = body;
    return st;
}

static Stmt* parse_stmt(Parser* P) {
    Token* t = peek(P);

    if (match(P, TK_LET) || match(P, TK_VOIDLET) || match(P, TK_SLET)) {
        TokenKind kw = prev(P)->kind;

        Token* nameTok = consume(P, TK_IDENT, "expected identifier after LET/VOIDLET/SLET");
        char* name = tok_text(P, nameTok);

        TypeSpec ts = parse_typespec_opt(P);

        consume(P, TK_EQ, "expected '=' in LET/VOIDLET/SLET");
        Expr* rhs = parse_expr(P);
        consume(P, TK_SEMI, "expected ';' after LET/VOIDLET/SLET");

        Stmt* st = new_stmt((kw==TK_LET)?ST_LET : (kw==TK_VOIDLET)?ST_VOIDLET : ST_SLET, t->line, t->col);
        st->as.let_stmt.name = name;
        st->as.let_stmt.ts   = ts;
        st->as.let_stmt.rhs  = rhs;
        return st;
    }

    if (match(P, TK_SET)) {
        Token* nameTok = consume(P, TK_IDENT, "expected identifier after SET");
        char* name = tok_text(P, nameTok);

        if (at(P, TK_ARROW)) {
            die_at(&P->S, peek(P)->line, peek(P)->col, "TypepointerError",
                   "Attempted to rewrite typepointer (use VOIDLET or VOID + LET)");
        }

        consume(P, TK_EQ, "expected '=' in SET");
        Expr* rhs = parse_expr(P);
        consume(P, TK_SEMI, "expected ';' after SET");

        Stmt* st = new_stmt(ST_SET, t->line, t->col);
        st->as.set_stmt.name = name;
        st->as.set_stmt.rhs  = rhs;
        return st;
    }

    if (match(P, TK_VOID)) {
        Token* nameTok = consume(P, TK_IDENT, "expected identifier after VOID");
        char* name = tok_text(P, nameTok);
        consume(P, TK_SEMI, "expected ';' after VOID");

        Stmt* st = new_stmt(ST_VOID, t->line, t->col);
        st->as.void_stmt.name = name;
        return st;
    }

    if (match(P, TK_NOTHING)) {
        consume(P, TK_SEMI, "expected ';' after NOTHING");
        return new_stmt(ST_NOTHING, t->line, t->col);
    }

    if (match(P, TK_RETURN)) {
        Expr* e = NULL;
        if (!at(P, TK_SEMI)) e = parse_expr(P);
        consume(P, TK_SEMI, "expected ';' after RETURN");
        Stmt* st = new_stmt(ST_RETURN, t->line, t->col);
        st->as.ret_stmt.expr = e;
        return st;
    }

    if (match(P, TK_IF)) return parse_if(P);
    if (match(P, TK_WHILE)) return parse_while(P);
    if (match(P, TK_FOR)) return parse_for(P);

    if (match(P, TK_SAY)) {
        Token* sayTok = prev(P);

        if (at(P, TK_LPAREN) && sayTok->adjacent_to_next) {
            consume(P, TK_LPAREN, "expected '('");
            Expr* e = parse_expr(P);
            consume(P, TK_RPAREN, "expected ')'");
            consume(P, TK_SEMI, "expected ';' after SAY(...)");
            Stmt* st = new_stmt(ST_SAY_EXPR, t->line, t->col);
            st->as.say_expr.expr = e;
            return st;
        }

        Token* nameTok = consume(P, TK_IDENT, "expected identifier after SAY (use SAY(expr) for expressions)");
        char* name = tok_text(P, nameTok);

        if (!at(P, TK_SEMI)) {
            die_at(&P->S, peek(P)->line, peek(P)->col, "SyntaxError",
                   "Invalid or unexpected token after 'SAY' (use SAY(expr) for expressions)");
        }
        consume(P, TK_SEMI, "expected ';' after SAY <ident>");

        Stmt* st = new_stmt(ST_SAY_IDENT, t->line, t->col);
        st->as.say_ident.name = name;
        return st;
    }
	
    if (at(P, TK_IDENT)) {
	    size_t save = P->pos;
	    Target tgt = parse_target(P);

	    if (match(P, TK_PLUSPLUS) || match(P, TK_MINUSMINUS)) {
		    TokenKind op = prev(P)->kind;
		    consume(P, TK_SEMI, "expected ';' after increment");

		    Stmt* st = new_stmt(ST_INCDEC, tgt.line, tgt.col);
		    st->as.incdec_stmt.target = tgt;
		    st->as.incdec-stmt.is_inc = (op == TK_PLUSPLUS);
		    return st;
	   }
	   
	   P->pos =save;
    }

    // expression statement (usually a call)
    if (at(P, TK_IDENT) || at(P, TK_ELLIPSIS) || at(P, TK_LPAREN) || at(P, TK_INT_LIT) || at(P, TK_STR_LIT) || at(P, TK_TRUE) || at(P, TK_FALSE) || at(P, TK_NULL) || at(P, TK_NOT)) {
        Expr* e = parse_expr(P);
        consume(P, TK_SEMI, "expected ';' after expression");
        Stmt* st = new_stmt(ST_EXPR, t->line, t->col);
        st->as.expr_stmt.expr = e;
        return st;
    }

    die_at(&P->S, t->line, t->col, "SyntaxError", "unexpected token in statement");
    return NULL;
}

static Target parse_target(Parser* P) {
	Token* t = peek(P);
	Target tgt;
	memset(&tgt, 0, sizeof(tgt));
	tgt.live = t->line;
	tgt.col = t->col;

	// base must be IDENT 4 now
	Token* nameTok = consume(P, TK_IDENT, "expected identifier as assignment target");
	char* base = tok_text(P, nameTok);

	if (match(P, TK_LBRACKET)) {
		Expr* idx = parse_expr(P);
		consume(P, TK_RBRACKET, "expected ']'");
		tgt.kind = TGT_INDEX;
		tgt.as.index.base_name = base;
		tgt.as.index.index = idx;
		return tgt;
	}

	tgt.kind = TGT_NAME;
	tgt.as.name.name = base;
	return tgt;
}

static AssignOp parse_assign_op(Parser* P) {
	if (match(P, TK_EQ)) return ASG_EQ;
	if (match(P, TK_PLUSEQ)) return ASG_ADD;
	if (match(P, TK_MINUSEQ)) return ASG_SUB;
	if (match(P, TK_STAREQ)) return ASG_MUL;
	if (match(P, TK_SLASHEQ)) return ASG_DIV;
	if (match(P, TK_PERCENTEQ)) return ASG_MOD;
	die_at(&P->S, peek(P)->line, peek(P)->col, "SyntaxError", "expected assignment operator");

static Stmt* parse_block(Parser* P) {
    consume(P, TK_LBRACE, "expected '{'");
    Stmt* head=NULL;
    Stmt* tail=NULL;

    while (!at(P, TK_RBRACE)) {
        if (at(P, TK_EOF)) die_at(&P->S, peek(P)->line, peek(P)->col, "SyntaxError", "unexpected EOF in block");
        Stmt* st = parse_stmt(P);
        if (!head) head=tail=st;
        else { tail->next = st; tail = st; }
    }
    consume(P, TK_RBRACE, "expected '}'");
    return head; // may be NULL (empty block)
}

static void parse_param_list(Parser* P, FunctionDef* f) {
    f->param_names = NULL;
    f->param_ts = NULL;
    f->param_count = 0;
    f->takes_only_null = false;

    f->has_varargs = false;
    f->varargs_ts.is_typed = false;
    f->varargs_ts.is_const_binding = false;
    f->varargs_ts.tag = TY_NONE;
    f->varargs_name = NULL;

    if (match(P, TK_RPAREN)) return;

    if (match(P, TK_NULL)) {
        f->takes_only_null = true;
        consume(P, TK_RPAREN, "expected ')'");
        return;
    }

    for (;;) {
        if (match(P, TK_ELLIPSIS)) {
            if (f->has_varargs) die_at(&P->S, prev(P)->line, prev(P)->col, "SyntaxError", "duplicate '...' parameter");
            f->has_varargs = true;
            f->varargs_name = xstrdup("...");
            f->varargs_ts = parse_typespec_opt(P); // ...->str etc.

            if (match(P, TK_COMMA)) {
                die_at(&P->S, prev(P)->line, prev(P)->col, "SyntaxError", "'...' must be the last parameter");
            }
            consume(P, TK_RPAREN, "expected ')'");
            return;
        }

        Token* nameTok = consume(P, TK_IDENT, "expected parameter name");
        char* pname = tok_text(P, nameTok);
        TypeSpec ts = parse_typespec_opt(P);

        f->param_names = (char**)xrealloc(f->param_names, (f->param_count+1)*sizeof(char*));
        f->param_ts    = (TypeSpec*)xrealloc(f->param_ts, (f->param_count+1)*sizeof(TypeSpec));
        f->param_names[f->param_count] = pname;
        f->param_ts[f->param_count] = ts;
        f->param_count++;

        if (match(P, TK_COMMA)) {
            if (at(P, TK_RPAREN)) break;
            continue;
        }
        break;
    }

    consume(P, TK_RPAREN, "expected ')'");
}

static FunctionDef parse_function(Parser* P) {
    Token* defTok = consume(P, TK_DEFINE, "expected DEFINE");
    consume(P, TK_FUNCTION, "expected FUNCTION after DEFINE");
    Token* nameTok = consume(P, TK_IDENT, "expected function name");
    char* name = tok_text(P, nameTok);

    consume(P, TK_LPAREN, "expected '(' after function name");

    FunctionDef f;
    memset(&f,0,sizeof(f));
    f.name = name;
    f.line = defTok->line;
    f.col  = defTok->col;

    parse_param_list(P, &f);

    f.ret_type = TY_NULL;
    if (match(P, TK_RARROW)) {
        Token* rt = consume(P, TK_IDENT, "expected return type after '>>'");
        char* rname = tok_text(P, rt);
        f.ret_type = type_from_name(rname);
        free(rname);
        if (f.ret_type == TY_NONE) die_at(&P->S, rt->line, rt->col, "TypeError", "unknown return type");
    }

    f.body = parse_block(P);

    if (!f.body && !f.takes_only_null) {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "expected block within '%s()' but got '', did you mean to put NOTHING in this function?",
            f.name);
        die_at(&P->S, defTok->line, defTok->col, "SyntaxError", buf);
    }

    return f;
}

Program parse_program(Parser* P) {
    Program prog;
    memset(&prog,0,sizeof(prog));

    Stmt* top_head=NULL;
    Stmt* top_tail=NULL;

    while (!at(P, TK_EOF)) {
        if (match(P, TK_SEMI)) continue;

        if (at(P, TK_DEFINE)) {
            FunctionDef f = parse_function(P);
            prog.funcs = (FunctionDef*)xrealloc(prog.funcs, (prog.nfuncs+1)*sizeof(FunctionDef));
            prog.funcs[prog.nfuncs++] = f;
            continue;
        }

        Stmt* st = parse_stmt(P);
        if (!top_head) top_head=top_tail=st;
        else { top_tail->next = st; top_tail = st; }
    }

    prog.top = top_head;
    return prog;
}