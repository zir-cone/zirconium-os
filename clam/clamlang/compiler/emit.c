#include "core.h"

// --------------------- Bc helpers ---------------------

static void bc_emit_u8(Chunk* c, uint8_t b){
    if (c->len+1 > c->cap) { c->cap = (c->cap==0)? 256 : c->cap*2; c->code = (uint8_t*)xrealloc(c->code, c->cap); }
    c->code[c->len++] = b;
}
static void bc_emit_u16(Chunk* c, uint16_t x){
    bc_emit_u8(c, (uint8_t)(x & 0xFF));
    bc_emit_u8(c, (uint8_t)((x >> 8) & 0xFF));
}
static void bc_emit_i32(Chunk* c, int32_t x){
    for (int i=0;i<4;i++) bc_emit_u8(c, (uint8_t)((uint32_t)x >> (8*i)));
}
static void bc_emit_i64(Chunk* c, int64_t x){
    for (int i=0;i<8;i++) bc_emit_u8(c, (uint8_t)((uint64_t)x >> (8*i)));
}

static size_t bc_here(Chunk* c){ return c->len; }
static void bc_patch_i32(Chunk* c, size_t at, int32_t value){
    for (int i=0;i<4;i++) c->code[at+i] = (uint8_t)((uint32_t)value >> (8*i));
}

// --------------------- Str pool ---------------------

static uint16_t intern_string(Bytecode* bc, const char* s) {
    for (size_t i=0;i<bc->str_count;i++) {
        if (strcmp(bc->str_pool[i], s)==0) return (uint16_t)i;
    }
    if (bc->str_count+1 > bc->str_cap) {
        bc->str_cap = (bc->str_cap==0)? 32 : bc->str_cap*2;
        bc->str_pool = (char**)xrealloc(bc->str_pool, bc->str_cap*sizeof(char*));
    }
    bc->str_pool[bc->str_count] = xstrdup(s);
    return (uint16_t)bc->str_count++;
}

// --------------------- name map per chubk ---------------------

static void map_put(Chunk* c, const char* name, uint16_t slot) {
    for (size_t i=0;i<c->nmap;i++) {
        if (strcmp(c->name_map[i], name)==0) { c->slot_map[i]=slot; return; }
    }
    if (c->nmap+1 > c->map_cap) {
        c->map_cap = (c->map_cap==0)? 32 : c->map_cap*2;
        c->name_map = (char**)xrealloc(c->name_map, c->map_cap*sizeof(char*));
        c->slot_map = (uint16_t*)xrealloc(c->slot_map, c->map_cap*sizeof(uint16_t));
    }
    c->name_map[c->nmap] = xstrdup(name);
    c->slot_map[c->nmap] = slot;
    c->nmap++;
}

static bool map_get(Chunk* c, const char* name, uint16_t* out_slot) {
    for (size_t i=0;i<c->nmap;i++) {
        if (strcmp(c->name_map[i], name)==0) { *out_slot = c->slot_map[i]; return true; }
    }
    return false;
}

static void map_remove_ordered(Chunk* c, const char* name) {
    for (size_t i=0;i<c->nmap;i++) {
        if (strcmp(c->name_map[i], name)==0) {
            free(c->name_map[i]);
            for (size_t j=i+1;j<c->nmap;j++) {
                c->name_map[j-1] = c->name_map[j];
                c->slot_map[j-1] = c->slot_map[j];
            }
            c->nmap--;
            return;
        }
    }
}

// --------------------- locals ---------------------

static uint16_t add_local(Chunk* c, const char* name, TypeSpec ts) {
    if (c->nlocals+1 > c->locals_cap) {
        c->locals_cap = (c->locals_cap==0)? 32 : c->locals_cap*2;
        c->locals_ts = (TypeSpec*)xrealloc(c->locals_ts, c->locals_cap*sizeof(TypeSpec));
        c->locals_name = (char**)xrealloc(c->locals_name, c->locals_cap*sizeof(char*));
        c->locals_is_live = (bool*)xrealloc(c->locals_is_live, c->locals_cap*sizeof(bool));
    }
    uint16_t slot = (uint16_t)c->nlocals++;
    c->locals_ts[slot] = ts;
    c->locals_name[slot] = xstrdup(name);
    c->locals_is_live[slot] = true;
    map_put(c, name, slot);
    return slot;
}

// --------------------- function index ---------------------

static int find_func(const Bytecode* bc, const char* name) {
    for (size_t i=0;i<bc->func_count;i++) {
        if (_stricmp(bc->func_names[i], name)==0) return (int)i;
    }
    return -1;
}

// --------------------- emit context ---------------------

typedef struct {
    // scope stack: each scope tracks declared names
    char*** scope_names;
    size_t* scope_counts;
    size_t* scope_caps;
    size_t scope_depth;

    // loop stack: hoisted SLET names live as long as loop lives
    char*** loop_names;
    uint16_t** loop_slots;
    size_t* loop_counts;
    size_t* loop_caps;
    size_t loop_depth;
} EmitCtx;

static void scope_push(EmitCtx* ec) {
    ec->scope_depth++;
    ec->scope_names = (char***)xrealloc(ec->scope_names, ec->scope_depth*sizeof(char**));
    ec->scope_counts = (size_t*)xrealloc(ec->scope_counts, ec->scope_depth*sizeof(size_t));
    ec->scope_caps = (size_t*)xrealloc(ec->scope_caps, ec->scope_depth*sizeof(size_t));
    ec->scope_names[ec->scope_depth-1] = NULL;
    ec->scope_counts[ec->scope_depth-1] = 0;
    ec->scope_caps[ec->scope_depth-1] = 0;
}

static void scope_record_name(EmitCtx* ec, const char* name) {
    if (ec->scope_depth == 0) return;
    size_t d = ec->scope_depth-1;
    if (ec->scope_counts[d]+1 > ec->scope_caps[d]) {
        ec->scope_caps[d] = (ec->scope_caps[d]==0)? 16 : ec->scope_caps[d]*2;
        ec->scope_names[d] = (char**)xrealloc(ec->scope_names[d], ec->scope_caps[d]*sizeof(char*));
    }
    ec->scope_names[d][ec->scope_counts[d]++] = xstrdup(name);
}

static bool loop_has_name(EmitCtx* ec, const char* name, uint16_t* out_slot) {
    for (size_t ld=ec->loop_depth; ld>0; ld--) {
        size_t d = ld-1;
        for (size_t i=0;i<ec->loop_counts[d];i++) {
            if (strcmp(ec->loop_names[d][i], name)==0) {
                *out_slot = ec->loop_slots[d][i];
                return true;
            }
        }
    }
    return false;
}

static void scope_pop(EmitCtx* ec, Chunk* c) {
    if (ec->scope_depth == 0) return;
    size_t d = ec->scope_depth-1;

    // remove declared names unless they are hoisted SLET still active in a loop
    for (size_t i=0;i<ec->scope_counts[d];i++) {
        uint16_t dummy=0;
        if (loop_has_name(ec, ec->scope_names[d][i], &dummy)) {
            free(ec->scope_names[d][i]);
            continue; // keep mapping alive
        }
        map_remove_ordered(c, ec->scope_names[d][i]);
        free(ec->scope_names[d][i]);
    }
    free(ec->scope_names[d]);

    ec->scope_depth--;
}

static void loop_push(EmitCtx* ec) {
    ec->loop_depth++;
    ec->loop_names = (char***)xrealloc(ec->loop_names, ec->loop_depth*sizeof(char**));
    ec->loop_slots = (uint16_t**)xrealloc(ec->loop_slots, ec->loop_depth*sizeof(uint16_t*));
    ec->loop_counts = (size_t*)xrealloc(ec->loop_counts, ec->loop_depth*sizeof(size_t));
    ec->loop_caps = (size_t*)xrealloc(ec->loop_caps, ec->loop_depth*sizeof(size_t));
    ec->loop_names[ec->loop_depth-1] = NULL;
    ec->loop_slots[ec->loop_depth-1] = NULL;
    ec->loop_counts[ec->loop_depth-1] = 0;
    ec->loop_caps[ec->loop_depth-1] = 0;
}

static void loop_pop(EmitCtx* ec) {
    if (ec->loop_depth == 0) return;
    size_t d = ec->loop_depth-1;
    for (size_t i=0;i<ec->loop_counts[d];i++) free(ec->loop_names[d][i]);
    free(ec->loop_names[d]);
    free(ec->loop_slots[d]);
    ec->loop_depth--;
}

static void loop_record_slet(EmitCtx* ec, const char* name, uint16_t slot) {
    if (ec->loop_depth == 0) return;
    size_t d = ec->loop_depth-1;

    if (ec->loop_counts[d]+1 > ec->loop_caps[d]) {
        ec->loop_caps[d] = (ec->loop_caps[d]==0)? 8 : ec->loop_caps[d]*2;
        ec->loop_names[d] = (char**)xrealloc(ec->loop_names[d], ec->loop_caps[d]*sizeof(char*));
        ec->loop_slots[d] = (uint16_t*)xrealloc(ec->loop_slots[d], ec->loop_caps[d]*sizeof(uint16_t));
    }
    ec->loop_names[d][ec->loop_counts[d]] = xstrdup(name);
    ec->loop_slots[d][ec->loop_counts[d]] = slot;
    ec->loop_counts[d]++;
}

/* ===== Emission forward decls ===== */

static void emit_expr(Bytecode* bc, Chunk* c, EmitCtx* ec, const Expr* e);
static void emit_stmt_list(Bytecode* bc, Chunk* c, EmitCtx* ec, const Stmt* st);

static bool is_builtin_name(const char* name, BuiltinId* out) {
    if (_stricmp(name,"say")==0)    { *out = BI_SAY; return true; }
    if (_stricmp(name,"length")==0) { *out = BI_LENGTH; return true; }
    if (_stricmp(name,"type")==0)   { *out = BI_TYPE; return true; }
    if (_stricmp(name,"push")==0)   { *out = BI_PUSH; return true; }
    if (_stricmp(name,"pop")==0)    { *out = BI_POP; return true; }
    if (_stricmp(name,"range")==0)  { *out = BI_RANGE; return true; }
    return false;
}

static void emit_assign_math(Bytecode* bc, Chunk* c, EmitCtx* ec, uint16_t slot, const Expr* rhs, AssignOp opkind) {
    (void)ec;
    // Load current value of target
    bc_emit_u8(c, OP_GET_LOCAL);
    bc_emit_u16(c, slot);

    // eval rhs
    emit_expr(bc, c, ec, rhs);

    // apply operator
    switch (opkind) {
        case ASG_ADD: bc_emit_u8(c, OP_ADD); break;
        case ASG_SUB: bc_emit_u8(c, OP_SUB); break;
        case ASG_MUL: bc_emit_u8(c, OP_MUL); break;
        case ASG_DIV: bc_emit_u8(c, OP_DIV); break;
        case ASG_MOD: bc_emit_u8(c, OP_MOD); break;
        default:
            fprintf(stderr, "internal: bad AssignOp\n");
            exit(1);
    }

    // stor back
    bc_emit_u8(c, OP_SET_LOCAL);
    bc_emit_u16(c, slot);
}

static void emit_expr(Bytecode* bc, Chunk* c, EmitCtx* ec, const Expr* e) {
    (void)ec;
    switch (e->kind) {
        case EX_INT:
            bc_emit_u8(c, OP_PUSH_INT); bc_emit_i64(c, e->as.ival);
            break;
        case EX_BOOL:
            bc_emit_u8(c, OP_PUSH_BOOL); bc_emit_u8(c, (uint8_t)(e->as.bval?1:0));
            break;
        case EX_NULL:
            bc_emit_u8(c, OP_PUSH_NULL);
            break;
        case EX_STR: {
            uint16_t idx = intern_string(bc, e->as.sval);
            bc_emit_u8(c, OP_PUSH_STR);
            bc_emit_u16(c, idx);
        } break;
        case EX_LIST: {
            // push each element value
            for (size_t i = 0; i < e->as.list.count; i++) {
                emit_expr(bc, c, ec, e->as.list.items[i]);
            }
            bc_emit_u8(c, OP_MAKE_LIST);
            bc_emit_u16(c, (uint16_t)e->as.list.count);
        } break;
        case EX_VAR: {
            uint16_t slot=0;
            if (!map_get(c, e->as.var.name, &slot)) {
                fprintf(stderr, "NameError: variable '%s' not defined\n", e->as.var.name);
                exit(1);
            }
            bc_emit_u8(c, OP_GET_LOCAL); bc_emit_u16(c, slot);
        } break;
        case EX_INDEX: {
            emit_expr(bc, c, ec, e->as.index.base);
            emit_expr(bc, c, ec, e->as.index.index);
            bc_emit_u8(c, OP_INDEX_GET);
        } break;
        case EX_UNARY: {
            emit_expr(bc, c, ec, e->as.unary.inner);
            if ((TokenKind)e->as.unary.op == TK_NOT) bc_emit_u8(c, OP_NOT);
            else { fprintf(stderr, "internal: unknown unary op\n"); exit(1); }
        } break;
        case EX_CALL: {
            BuiltinId bid=0;
            if (is_builtin_name(e->as.call.callee, &bid)) {
                for (size_t i=0;i<e->as.call.argc;i++) emit_expr(bc, c, ec, e->as.call.args[i]);
                bc_emit_u8(c, OP_CALL_BUILTIN);
                bc_emit_u16(c, (uint16_t)bid);
                bc_emit_u16(c, (uint16_t)e->as.call.argc);
                break;
            }

            int fi = find_func(bc, e->as.call.callee);
            if (fi < 0) {
                fprintf(stderr, "NameError: function '%s' not defined\n", e->as.call.callee);
                exit(1);
            }
            for (size_t i=0;i<e->as.call.argc;i++) emit_expr(bc, c, ec, e->as.call.args[i]);
            bc_emit_u8(c, OP_CALL);
            bc_emit_u16(c, (uint16_t)fi);
            bc_emit_u16(c, (uint16_t)e->as.call.argc);
        } break;
        case EX_BIN: {
            emit_expr(bc, c, ec, e->as.bin.a);
            emit_expr(bc, c, ec, e->as.bin.b);
            switch ((TokenKind)e->as.bin.op) {
                case TK_PLUS:  bc_emit_u8(c, OP_ADD);   break;
                case TK_MINUS: bc_emit_u8(c, OP_SUB);   break;
                case TK_STAR:  bc_emit_u8(c, OP_MUL);   break;
                case TK_SLASH: bc_emit_u8(c, OP_DIV);   break;
                case TK_PERCENT: bc_emit_u8(c, OP_MOD); break;

                case TK_EQEQ:  bc_emit_u8(c, OP_EQEQ);  break;
                case TK_NEQ:   bc_emit_u8(c, OP_NEQ);   break;
                case TK_LT:    bc_emit_u8(c, OP_LT);    break;
                case TK_LTE:   bc_emit_u8(c, OP_LTE);   break;
                case TK_GT:    bc_emit_u8(c, OP_GT);    break;
                case TK_GTE:   bc_emit_u8(c, OP_GTE);   break;
 
                case TK_AND:   bc_emit_u8(c, OP_AND);   break;
                case TK_OR:    bc_emit_u8(c, OP_OR);    break;
                case TK_NAND:  bc_emit_u8(c, OP_NAND);  break;
                case TK_NOR:   bc_emit_u8(c, OP_NOR);   break;

                default:
                    fprintf(stderr, "internal: unsupported binop\n");
                    exit(1);
            }
        } break;
        default:
            fprintf(stderr, "internal: bad expr kind\n");
            exit(1);
    }
}

static void emit_if(Bytecode* bc, Chunk* c, EmitCtx* ec, const Stmt* st) {
    emit_expr(bc, c, ec, st->as.if_stmt.cond);

    bc_emit_u8(c, OP_JMP_IF_FALSE);
    size_t jfalse_at = bc_here(c);
    bc_emit_i32(c, 0);

    scope_push(ec);
    emit_stmt_list(bc, c, ec, st->as.if_stmt.then_block);
    scope_pop(ec, c);

    bc_emit_u8(c, OP_JMP);
    size_t jend_at = bc_here(c);
    bc_emit_i32(c, 0);

    size_t else_ip = bc_here(c);
    int32_t rel_false = (int32_t)(else_ip - (jfalse_at + 4));
    bc_patch_i32(c, jfalse_at, rel_false);

    if (st->as.if_stmt.else_branch) {
        if (st->as.if_stmt.else_is_if) {
            emit_if(bc, c, ec, st->as.if_stmt.else_branch);
        } else {
            scope_push(ec);
            emit_stmt_list(bc, c, ec, st->as.if_stmt.else_branch);
            scope_pop(ec, c);
        }
    }

    size_t end_ip = bc_here(c);
    int32_t rel_end = (int32_t)(end_ip - (jend_at + 4));
    bc_patch_i32(c, jend_at, rel_end);
}

static void emit_while(Bytecode* bc, Chunk* c, EmitCtx* ec, const Stmt* st) {
    loop_push(ec);

    size_t loop_start = bc_here(c);

    emit_expr(bc, c, ec, st->as.while_stmt.cond);

    bc_emit_u8(c, OP_JMP_IF_FALSE);
    size_t jend_at = bc_here(c);
    bc_emit_i32(c, 0);

    scope_push(ec);
    emit_stmt_list(bc, c, ec, st->as.while_stmt.body);
    scope_pop(ec, c);

    bc_emit_u8(c, OP_JMP);
    int32_t rel_back = (int32_t)(loop_start - (bc_here(c) + 4));
    bc_emit_i32(c, rel_back);

    size_t end_ip = bc_here(c);
    int32_t rel_end = (int32_t)(end_ip - (jend_at + 4));
    bc_patch_i32(c, jend_at, rel_end);

    loop_pop(ec);
}

static void emit_stmt(Bytecode* bc, Chunk* c, EmitCtx* ec, const Stmt* st) {
    switch (st->kind) {
        case ST_LET: {
            uint16_t dummy=0;
            if (map_get(c, st->as.let_stmt.name, &dummy)) {
                fprintf(stderr, "TypeError: attempted to redeclare '%s' (use VOID/VOIDLET)\n", st->as.let_stmt.name);
                exit(1);
            }
            uint16_t slot = add_local(c, st->as.let_stmt.name, st->as.let_stmt.ts);
            scope_record_name(ec, st->as.let_stmt.name);

            emit_expr(bc, c, ec, st->as.let_stmt.rhs);
            bc_emit_u8(c, OP_SET_LOCAL); bc_emit_u16(c, slot);
        } break;

        case ST_VOIDLET: {
            map_remove_ordered(c, st->as.let_stmt.name);
            uint16_t slot = add_local(c, st->as.let_stmt.name, st->as.let_stmt.ts);
            scope_record_name(ec, st->as.let_stmt.name);

            emit_expr(bc, c, ec, st->as.let_stmt.rhs);
            bc_emit_u8(c, OP_SET_LOCAL); bc_emit_u16(c, slot);
        } break;

        case ST_SLET: {
            if (ec->loop_depth == 0) {
                fprintf(stderr, "SyntaxError: SLET may only be used within a loop\n");
                exit(1);
            }

            uint16_t slot=0;
            if (!loop_has_name(ec, st->as.let_stmt.name, &slot)) {
                uint16_t dummy=0;
                if (map_get(c, st->as.let_stmt.name, &dummy)) {
                    fprintf(stderr, "TypeError: attempted to redeclare '%s' (use VOID/VOIDLET)\n", st->as.let_stmt.name);
                    exit(1);
                }
                slot = add_local(c, st->as.let_stmt.name, st->as.let_stmt.ts);
                scope_record_name(ec, st->as.let_stmt.name);
                loop_record_slet(ec, st->as.let_stmt.name, slot);
            }

            emit_expr(bc, c, ec, st->as.let_stmt.rhs);
            bc_emit_u8(c, OP_SET_LOCAL); bc_emit_u16(c, slot);
        } break;

        case ST_SET: {
            Target tgt = st->as.set_stmt.target;
            AssignOp op = st->as.set_stmt.op;

            if (tgt.kind == TGT_NAME) {
                uint16_t slot=0;
                if (!map_get(c, tgt.as.name.name, &slot)) {
                    fprintf(stderr, "NameError: variable '%s' not defined\n", tgt.as.name.name);
                    exit(1);
                }

                if (op == ASG_EQ) {
                    emit_expr(bc, c, ec, st->as.set_stmt.rhs);
                    bc_emit_u8(c, OP_SET_LOCAL);
                    bc_emit_u16(c, slot);
                    break;
                }

                emit_assign_math(bc, c, ec, slot, st->as.set_stmt.rhs, op);
                break;
            }

            if (tgt.kind == TGT_INDEX) {
                uint16_t base_slot=0;
                if (!map_get(c, tgt.as.index.base_name, &base_slot)) {
                    fprintf(stderr, "NameError: variable '%s' not defined\n", tgt.as.index.base_name);
                    exit(1);
                }

                if (op == ASG_EQ) {
                    bc_emit_u8(c, OP_GET_LOCAL); bc_emit_u16(c, base_slot);
                    emit_expr(bc, c, ec, tgt.as.index.index);
                    emit_expr(bc, c, ec, st->as.set_stmt.rhs);
                    bc_emit_u8(c, OP_INDEX_SET);
                    break;
                }

                bc_emit_u8(c, OP_GET_LOCAL); bc_emit_u16(c, base_slot);
                emit_expr(bc, c, ec, tgt.as.index.index);
                bc_emit_u8(c, OP_GET_LOCAL); bc_emit_u16(c, base_slot);
                emit_expr(bc, c, ec, tgt.as.index.index);
                bc_emit_u8(c, OP_INDEX_GET);

                emit_expr(bc, c, ec, st->as.set_stmt.rhs);
                switch (op) {
                    case ASG_ADD: bc_emit_u8(c, OP_ADD); break;
                    case ASG_SUB: bc_emit_u8(c, OP_SUB); break;
                    case ASG_MUL: bc_emit_u8(c, OP_MUL); break;
                    case ASG_DIV: bc_emit_u8(c, OP_DIV); break;
                    case ASG_MOD: bc_emit_u8(c, OP_MOD); break;
                    default: fprintf(stderr, "internal: bad assign op\n"); exit(1);
                }
                
                bc_emit_u8(c, OP_INDEX_SET);
                break;
            }
        } break;

        case ST_INCDEC: {
            Target tgt = st->as.incdec_stmt.target;
            bool inc = st->as.incdec_stmt.is_inc;

            // build rhs = target +/- 1 and assign back
            Expr one;
            memset(&one, 0, sizeof(one));
            one.kind    = EX_INT;
            one.line    = st->line;
            one.col     = st->col;
            one.as.ival = 1;

            if (tgt.kind == TGT_NAME) {
                uint16_t slot=0;
                if (!map_get(c, tgt.as.name.name, &slot)) {
                    fprintf(stderr, "NameError: variable '%s' not defined\n", tgt.as.name.name);
                    exit(1);
                }
                bc_emit_u8(c, OP_GET_LOCAL);
                bc_emit_u16(c, slot);
                emit_expr(bc, c, ec, &one);
                bc_emit_u8(c, inc ? OP_ADD : OP_SUB);
                bc_emit_u8(c, OP_SET_LOCAL);
                bc_emit_u16(c, slot);
                break;
            }

            if (tgt.kind == TGT_INDEX) {
                uint16_t base_slot=0;
                if (!map_get(c, tgt.as.index.base_name, &base_slot)) {
                    fprintf(stderr, "NameError: variable '%s' not defined\n", tgt.as.index.base_name);
                    exit(1);
                }

                // SET base[idx] = base[idx] +/- 1
                bc_emit_u8(c, OP_GET_LOCAL);
                bc_emit_u16(c, base_slot);
                emit_expr(bc, c, ec, tgt.as.index.index);

                bc_emit_u8(c, OP_GET_LOCAL);
                bc_emit_u16(c, base_slot);
                emit_expr(bc, c, ec, tgt.as.index.index);
                bc_emit_u8(c, OP_INDEX_GET);

                emit_expr(bc, c, ec, &one);
                bc_emit_u8(c, inc ? OP_ADD : OP_SUB);

                bc_emit_u8(c, OP_INDEX_SET);
                break;
            }
        } break;

        case ST_VOID: {
            uint16_t slot=0;
            if (!map_get(c, st->as.void_stmt.name, &slot)) {
                fprintf(stderr, "NameError: variable '%s' not defined\n", st->as.void_stmt.name);
                exit(1);
            }
            map_remove_ordered(c, st->as.void_stmt.name);
            bc_emit_u8(c, OP_VOID_LOCAL); bc_emit_u16(c, slot);
        } break;

        case ST_SAY_IDENT: {
            uint16_t slot=0;
            if (!map_get(c, st->as.say_ident.name, &slot)) {
                fprintf(stderr, "NameError: variable '%s' not defined\n", st->as.say_ident.name);
                exit(1);
            }
            bc_emit_u8(c, OP_GET_LOCAL); bc_emit_u16(c, slot);
            bc_emit_u8(c, OP_CALL_BUILTIN);
            bc_emit_u16(c, (uint16_t)BI_SAY);
            bc_emit_u16(c, 1);
            bc_emit_u8(c, OP_POP); // builtin leaves Null on stack
        } break;

        case ST_SAY_EXPR: {
            emit_expr(bc, c, ec, st->as.say_expr.expr);
            bc_emit_u8(c, OP_CALL_BUILTIN);
            bc_emit_u16(c, (uint16_t)BI_SAY);
            bc_emit_u16(c, 1);
            bc_emit_u8(c, OP_POP);
        } break;

        case ST_NOTHING:
            break;

        case ST_EXPR: {
            emit_expr(bc, c, ec, st->as.expr_stmt.expr);
            bc_emit_u8(c, OP_POP);
        } break;

        case ST_IF:
            emit_if(bc, c, ec, st);
            break;

        case ST_WHILE:
            emit_while(bc, c, ec, st);
            break;

        case ST_FOR: {
            loop_push(ec);

            // init
            scope_push(ec);
            if (st->as.for_stmt.init) emit_stmt(bc, c, ec, st->as.for_stmt.init);

            // loop start
            size_t loop_start = bc_here(c);

            // condition: iter < limit
            Expr iterVar;
            memset(&iterVar,0,sizeof(iterVar));
            iterVar.kind = EX_VAR;
            iterVar.line = st->line; iterVar.col = st->col;
            iterVar.as.var.name = st->as.for_stmt.iter_name;

            emit_expr(bc, c, ec, &iterVar);
            emit_expr(bc, c, ec, st->as.for_stmt.limit);
            bc_emit_u8(c, OP_LT);

            bc_emit_u8(c, OP_JMP_IF_FALSE);
            size_t jend_at = bc_here(c);
            bc_emit_i32(c, 0);

            // body
            scope_push(ec);
            emit_stmt_list(bc, c, ec, st->as.for_stmt.body);
            scope_pop(ec, c);

            // step
            if (st->as.for_stmt.step) emit_stmt(bc, c, ec, st->as.for_stmt.step);

            // jump back
            bc_emit_u8(c, OP_JMP);
            int32_t rel_back = (int32_t)(loop_start - (bc_here(c) + 4));
            bc_emit_i32(c, rel_back);

            // end
            size_t end_ip = bc_here(c);
            int32_t rel_end = (int32_t)(end_ip - (jend_at + 4));
            bc_patch_i32(c, jend_at, rel_end);

            scope_pop(ec, c);
            loop_pop(ec);
        } break;

        case ST_RETURN: {
            if (st->as.ret_stmt.expr) emit_expr(bc, c, ec, st->as.ret_stmt.expr);
            else bc_emit_u8(c, OP_PUSH_NULL);
            bc_emit_u8(c, OP_RET);
        } break;

        default:
            fprintf(stderr, "internal: unsupported stmt\n");
            exit(1);
    }
}

static void emit_stmt_list(Bytecode* bc, Chunk* c, EmitCtx* ec, const Stmt* st) {
    for (const Stmt* cur=st; cur; cur=cur->next) emit_stmt(bc, c, ec, cur);
}

static void init_chunk(Chunk* c) {
    memset(c,0,sizeof(*c));
}

Bytecode emit_bytecode(const Program* prog, const Source* S) {
    (void)S;

    Bytecode bc;
    memset(&bc,0,sizeof(bc));

    bc.func_count = prog->nfuncs;
    bc.funcs = (Chunk*)xmalloc(sizeof(Chunk)*bc.func_count);
    bc.func_names = (char**)xmalloc(sizeof(char*)*bc.func_count);
    bc.func_ret = (TypeTag*)xmalloc(sizeof(TypeTag)*bc.func_count);
    bc.func_param_ts = (TypeSpec**)xmalloc(sizeof(TypeSpec*)*bc.func_count);
    bc.func_param_count = (size_t*)xmalloc(sizeof(size_t)*bc.func_count);

    bc.func_has_varargs = (bool*)xmalloc(sizeof(bool)*bc.func_count);
    bc.func_varargs_ts = (TypeSpec*)xmalloc(sizeof(TypeSpec)*bc.func_count);
    bc.func_fixed_param_count = (size_t*)xmalloc(sizeof(size_t)*bc.func_count);

    for (size_t i=0;i<bc.func_count;i++) {
        bc.func_names[i] = xstrdup(prog->funcs[i].name);
        bc.func_ret[i] = prog->funcs[i].ret_type;

        bc.func_has_varargs[i] = prog->funcs[i].has_varargs;
        bc.func_varargs_ts[i] = prog->funcs[i].varargs_ts;
        bc.func_fixed_param_count[i] = prog->funcs[i].param_count;

        bc.func_param_count[i] = prog->funcs[i].param_count;
        if (prog->funcs[i].param_count) {
            bc.func_param_ts[i] = (TypeSpec*)xmalloc(sizeof(TypeSpec)*prog->funcs[i].param_count);
            memcpy(bc.func_param_ts[i], prog->funcs[i].param_ts, sizeof(TypeSpec)*prog->funcs[i].param_count);
        } else bc.func_param_ts[i] = NULL;

        init_chunk(&bc.funcs[i]);
    }

    EmitCtx ec;
    memset(&ec,0,sizeof(ec));

    // emit functions
    for (size_t i=0;i<bc.func_count;i++) {
        const FunctionDef* f = &prog->funcs[i];
        Chunk* c = &bc.funcs[i];

        scope_push(&ec);

        for (size_t p=0;p<f->param_count;p++) {
            add_local(c, f->param_names[p], f->param_ts[p]);
            scope_record_name(&ec, f->param_names[p]);
        }

        if (f->has_varargs) {
            // reserve a local binding for "..." which will be a list at runtime
            TypeSpec ts = {0};
            ts.is_typed = true;
            ts.is_const_binding = false;
            ts.tag = TY_LIST;
            add_local(c, "...", ts);
            scope_record_name(&ec, "...");
        }

        emit_stmt_list(&bc, c, &ec, f->body);

        // implicit return Null
        bc_emit_u8(c, OP_PUSH_NULL);
        bc_emit_u8(c, OP_RET);
        bc_emit_u8(c, OP_HALT);

        scope_pop(&ec, c);
    }

    // emit module body
    init_chunk(&bc.module);
    scope_push(&ec);
    emit_stmt_list(&bc, &bc.module, &ec, prog->top);

    bc_emit_u8(&bc.module, OP_PUSH_NULL);
    bc_emit_u8(&bc.module, OP_RET);
    bc_emit_u8(&bc.module, OP_HALT);
    scope_pop(&ec, &bc.module);

    return bc;
}