#include "../../compiler/core.h"

/* ===== GC ===== */

static void* vm_realloc(VM* vm, void* p, size_t old, size_t new_sz) {
    vm->bytes_allocated += (new_sz > old) ? (new_sz - old) : 0;
    void* r = realloc(p, new_sz);
    if (!r) { fprintf(stderr, "fatal: out of memory\n"); exit(1); }
    return r;
}

static ObjStr* new_string(VM* vm, const char* s, size_t len) {
    ObjStr* o = (ObjStr*)vm_realloc(vm, NULL, 0, sizeof(ObjStr));
    o->obj.kind = OBJ_STR;
    o->obj.marked = false;
    o->obj.next = vm->objects;
    vm->objects = (Obj*)o;

    o->len = len;
    o->chars = (char*)vm_realloc(vm, NULL, 0, len + 1);
    memcpy(o->chars, s, len);
    o->chars[len] = 0;
    return o;
}

static void list_ensure(VM* vm, ObjList* L, size_t need_len) {
  if (need_len <= L->cap) return;
  size_t newcap = (L->cap == 0) ? 4 : L->cap;
  while (newcap < need_len) newcap *= 2;
  L->items = (Value*)vm_realloc(vm, L->items, sizeof(Value)*L->cap, sizeof(Value)*newcap);
  L->cap = newcap;
}

static ObjList* new_list(VM* vm, size_t cap) {
    ObjList* L = (ObjList*)vm_realloc(vm, NULL, 0, sizeof(ObjList));
    L->obj.kind = OBJ_LIST;
    L->obj.marked = false;
    L->obj.next = vm->objects;
    vm->objects = (Obj*)L;

    L->len = 0;
    L->cap = cap ? cap : 1;
    L->items = (Value*)vm_realloc(vm, NULL, 0, sizeof(Value)*L->cap);
    return L;
}

static void mark_obj(Obj* o);

static void mark_value(Value v) {
    if (v.tag == V_OBJ) mark_obj(v.as.obj);
}

static void mark_obj(Obj* o) {
    if (!o || o->marked) return;
    o->marked = true;

    if (o->kind == OBJ_LIST) {
        ObjList* L = (ObjList*)o;
        for (size_t i=0;i<L->len;i++) mark_value(L->items[i]);
    }
}

static void gc_mark_roots(VM* vm) {
    for (size_t i = 0; i < vm->sp; i++) mark_value(vm->stack[i]);
    for (size_t f = 0; f < vm->fp; f++) {
        Frame* fr = &vm->frames[f];
        for (size_t i = 0; i < fr->nlocals; i++) mark_value(fr->locals[i]);
    }
}

static void gc_sweep(VM* vm) {
    Obj* prev = NULL;
    Obj* o = vm->objects;

    while (o) {
        if (o->marked) {
            o->marked = false;
            prev = o;
            o = o->next;
            continue;
        }

        Obj* unreached = o;
        o = o->next;

        if (prev) prev->next = o;
        else vm->objects = o;

        if (unreached->kind == OBJ_STR) {
            ObjStr* s = (ObjStr*)unreached;
            free(s->chars);
            free(s);
        } else if (unreached->kind == OBJ_LIST) {
            ObjList* L = (ObjList*)unreached;
            free(L->items);
            free(L);
        } else {
            free(unreached);
        }
    }
}

static void gc_collect(VM* vm) {
    gc_mark_roots(vm);
    gc_sweep(vm);
    vm->next_gc = vm->bytes_allocated * 2 + 1024 * 1024;
}

static void maybe_gc(VM* vm) {
    if (vm->bytes_allocated > vm->next_gc) gc_collect(vm);
}

/* ===== Stack helpers ===== */

static void stack_ensure(VM* vm, size_t need) {
    if (vm->sp + need > vm->stack_cap) {
        size_t old_bytes = vm->stack_cap * sizeof(Value);
        vm->stack_cap = (vm->stack_cap == 0) ? 256 : vm->stack_cap * 2;
        vm->stack = (Value*)vm_realloc(vm, vm->stack, old_bytes, vm->stack_cap * sizeof(Value));
    }
}

static void push(VM* vm, Value v) { stack_ensure(vm, 1); vm->stack[vm->sp++] = v; }

static Value pop(VM* vm) {
    if (vm->sp == 0) { fprintf(stderr, "VMError: stack underflow\n"); exit(1); }
    return vm->stack[--vm->sp];
}

/* ===== Byte readers ===== */

static uint16_t read_u16(const uint8_t* code, size_t* ip) {
    uint16_t lo = code[(*ip)++];
    uint16_t hi = code[(*ip)++];
    return (uint16_t)(lo | (hi << 8));
}

static int32_t read_i32(const uint8_t* code, size_t* ip) {
    uint32_t x = 0;
    for (int i = 0; i < 4; i++) x |= ((uint32_t)code[(*ip)++]) << (8 * i);
    return (int32_t)x;
}

static int64_t read_i64(const uint8_t* code, size_t* ip) {
    uint64_t x = 0;
    for (int i = 0; i < 8; i++) x |= ((uint64_t)code[(*ip)++]) << (8 * i);
    return (int64_t)x;
}

/* ===== Builtins ===== */

static void builtin_say(Value v) {
  switch (v.tag) {
    case V_INT:  printf("%lld\n", (long long)v.as.i); break;
    case V_BOOL: printf("%s\n", v.as.b ? "true" : "false"); break;
    case V_NULL: printf("Null\n"); break;
    case V_OBJ: {
      Obj* o = v.as.obj;
      if (o && o->kind == OBJ_STR) printf("%s\n", ((ObjStr*)o)->chars);
      else if (o && o->kind == OBJ_LIST) {
        ObjList* L = (ObjList*)o;
        printf("[");
        for (size_t i=0; i<L->len; i++) {
          Value it = L->items[i];
          if (it.tag == V_INT) printf("%lld", (long long)it.as.i);
          else if (it.tag == V_BOOL) printf("%s", it.as.b ? "true" : "false");
          else if (it.tag == V_NULL) printf("Null");
          else if (it.tag == V_OBJ && it.as.obj && it.as.obj->kind == OBJ_STR) printf("\"%s\"", ((ObjStr*)it.as.obj)->chars);
          else if (it.tag == V_OBJ && it.as.obj && it.as.obj->kind == OBJ_LIST) printf("<list>");
          else printf("<obj>");
          if (i+1 < L->len) printf(", ");
        }
        printf("]\n");
      }
      else printf("<obj>\n");
    } break;
  default: printf("<unknown>\n"); break;
  }
}

static Value builtin_length(Value v) {
    if (v.tag == V_OBJ && v.as.obj) {
        if (v.as.obj->kind == OBJ_STR) return VInt((int64_t)((ObjStr*)v.as.obj)->len);
        if (v.as.obj->kind == OBJ_LIST) return VInt((int64_t)((ObjList*)v.as.obj)->len);
    }
    fprintf(stderr, "TypeError: LENGTH expects str or list\n");
    exit(1);
}

static Value builtin_type(VM* vm, Value v) {
  const char* t = "unknown";
  if (v.tag == V_INT) t = "int";
  else if (v.tag == V_BOOL) t = "bool";
  else if (v.tag == V_NULL) t = "Null";
  else if (v.tag == V_OBJ && v.as.obj) {
    if (v.as.obj->kind == OBJ_STR) t = "str";
    else if (v.as.obj->kind == OBJ_LIST) t = "list";
  }
  ObjStr* s = new_string(vm, t, strlen(t));
  maybe_gc(vm);
  return VObj((Obj*)s);
}

static Value builtin_range(VM* vm, Value v) {
  if (v.tag != V_INT) { fprintf(stderr, "TypeError: RANGE expects int\n"); exit(1); }
  int64_t n = v.as.i;
  if (n < 0) { fprintf(stderr, "ValueError: RANGE expects non-negative int\n"); exit(1); }
  ObjList* L = new_list(vm, (size_t)n ? (size_t)n : 1);
  list_ensure(vm, L, (size_t)n);
  L->len = (size_t)n;
  for (int64_t i=0; i<n; i++) L->items[(size_t)i] = VInt(i);
  maybe_gc(vm);
  return VObj((Obj*)L);
}

static Value builtin_push(VM* vm, Value listv, Value item) {
  if(!(listv.tag == V_OBJ && listv.as.obj && listv.as.obj->kind == OBJ_LIST)) {
    fprintf(stderr, "TypeError: PUSH expects list as first arg\n");
    exit(1);
  }
  ObjList* L = (ObjList*)listv.as.obj;
  list_ensure(vm, L, L->len + 1);
  L->items[L->len++] = item;
  maybe_gc(vm);
  return VNull();
}

static Value builtin_pop(VM* vm, Value listv) {
  if (!(listv.tag == V_OBJ && listv.as.obj && listv.as.obj->kind == OBJ_LIST)) {
    fprintf(stderr, "TypeError: POP expects list\n");
    exit(1);
  }
  ObjList* L = (ObjList*)listv.as.obj;
  if (L->len == 0) return VNull();
  Value v = L->items[L->len - 1];
  L->len--;
  return v;
}
static bool is_falsey(Value v) {
    if (v.tag == V_BOOL) return !v.as.b;
    if (v.tag == V_NULL) return true;
    return false;
}

/* ===== Type checks ===== */

static void typecheck_store(const Chunk* c, uint16_t idx, Value v) {
    TypeSpec ts = c->locals_ts[idx];

    if (ts.is_const_binding) {
        fprintf(stderr, "TypeError: Attempted to rewrite read-only const variable '%s'\n", c->locals_name[idx]);
        exit(1);
    }

    if (!ts.is_typed) return;

    switch (ts.tag) {
        case TY_INT:
            if (v.tag != V_INT) { fprintf(stderr, "TypeError: cannot assign non-int to typed int '%s'\n", c->locals_name[idx]); exit(1); }
            break;
        case TY_BOOL:
            if (v.tag != V_BOOL) { fprintf(stderr, "TypeError: cannot assign non-bool to typed bool '%s'\n", c->locals_name[idx]); exit(1); }
            break;
        case TY_STR:
            if (!(v.tag == V_OBJ && v.as.obj && v.as.obj->kind == OBJ_STR)) { fprintf(stderr, "TypeError: cannot assign non-str to typed str '%s'\n", c->locals_name[idx]); exit(1); }
            break;
        case TY_LIST:
            if (!(v.tag == V_OBJ && v.as.obj && v.as.obj->kind == OBJ_LIST)) { fprintf(stderr, "TypeError: cannot assign non-list to typed list '%s'\n", c->locals_name[idx]); exit(1); }
            break;
        case TY_NULL:
            if (v.tag != V_NULL) { fprintf(stderr, "TypeError: cannot assign non-Null to typed Null '%s'\n", c->locals_name[idx]); exit(1); }
            break;
        case TY_ANY:
            break;
        default:
            break;
    }
}

static void enforce_return_type(const VM* vm, uint16_t func_index, Value v) {
    TypeTag rt = vm->bc.func_ret[func_index];

    if (rt == TY_ANY) return;

    if (rt == TY_NULL) { if (v.tag != V_NULL) { fprintf(stderr, "TypeError: function returned non-Null\n"); exit(1); } return; }
    if (rt == TY_INT)  { if (v.tag != V_INT)  { fprintf(stderr, "TypeError: function returned non-int\n"); exit(1); } return; }
    if (rt == TY_BOOL) { if (v.tag != V_BOOL) { fprintf(stderr, "TypeError: function returned non-bool\n"); exit(1); } return; }
    if (rt == TY_STR)  {
        if (!(v.tag == V_OBJ && v.as.obj && v.as.obj->kind == OBJ_STR)) { fprintf(stderr, "TypeError: function returned non-str\n"); exit(1); }
        return;
    }
    if (rt == TY_LIST) {
        if (!(v.tag == V_OBJ && v.as.obj && v.as.obj->kind == OBJ_LIST)) { fprintf(stderr, "TypeError: function returned non-list\n"); exit(1); }
        return;
    }
}

static void enforce_param_types_fixed(const VM* vm, uint16_t func_index, Value* args, uint16_t argc) {
    size_t expected = vm->bc.func_param_count[func_index];
    if (argc != expected) {
        fprintf(stderr, "TypeError: function '%s' expects %zu args but got %u\n",
            vm->bc.func_names[func_index], expected, (unsigned)argc);
        exit(1);
    }

    TypeSpec* pts = vm->bc.func_param_ts[func_index];
    for (size_t i = 0; i < expected; i++) {
        if (!pts[i].is_typed) continue;
        TypeTag t = pts[i].tag;
        Value v = args[i];

        if (t == TY_ANY) continue;
        if (t == TY_INT && v.tag != V_INT) { fprintf(stderr, "TypeError: arg %zu must be int\n", i); exit(1); }
        if (t == TY_BOOL && v.tag != V_BOOL) { fprintf(stderr, "TypeError: arg %zu must be bool\n", i); exit(1); }
        if (t == TY_NULL && v.tag != V_NULL) { fprintf(stderr, "TypeError: arg %zu must be Null\n", i); exit(1); }
        if (t == TY_STR) {
            if (!(v.tag == V_OBJ && v.as.obj && v.as.obj->kind == OBJ_STR)) { fprintf(stderr, "TypeError: arg %zu must be str\n", i); exit(1); }
        }
        if (t == TY_LIST) {
            if (!(v.tag == V_OBJ && v.as.obj && v.as.obj->kind == OBJ_LIST)) { fprintf(stderr, "TypeError: arg %zu must be list\n", i); exit(1); }
        }
    }
}

static void enforce_one_type(TypeSpec ts, Value v, const char* what) {
    if (!ts.is_typed) return;
    if (ts.tag == TY_ANY) return;

    if (ts.tag == TY_INT && v.tag != V_INT) { fprintf(stderr, "TypeError: %s must be int\n", what); exit(1); }
    if (ts.tag == TY_BOOL && v.tag != V_BOOL) { fprintf(stderr, "TypeError: %s must be bool\n", what); exit(1); }
    if (ts.tag == TY_NULL && v.tag != V_NULL) { fprintf(stderr, "TypeError: %s must be Null\n", what); exit(1); }
    if (ts.tag == TY_STR) {
        if (!(v.tag == V_OBJ && v.as.obj && v.as.obj->kind == OBJ_STR)) { fprintf(stderr, "TypeError: %s must be str\n", what); exit(1); }
    }
    if (ts.tag == TY_LIST) {
        if (!(v.tag == V_OBJ && v.as.obj && v.as.obj->kind == OBJ_LIST)) { fprintf(stderr, "TypeError: %s must be list\n", what); exit(1); }
    }
}

/* ===== Frames ===== */

static void frames_ensure(VM* vm, size_t need_more) {
    if (vm->frame_cap == 0) {
        vm->frame_cap = 64;
        vm->frames = (Frame*)xmalloc(sizeof(Frame) * vm->frame_cap);
    }
    if (vm->fp + need_more > vm->frame_cap) {
        vm->frame_cap *= 2;
        vm->frames = (Frame*)xrealloc(vm->frames, sizeof(Frame) * vm->frame_cap);
    }
}

static void push_frame(VM* vm, Chunk* chunk, bool is_function, uint16_t func_index) {
    frames_ensure(vm, 1);

    Frame* fr = &vm->frames[vm->fp++];
    fr->chunk = chunk;
    fr->ip = 0;
    fr->is_function = is_function;
    fr->func_index = func_index;

    fr->nlocals = chunk->nlocals;
    fr->locals = (Value*)xmalloc(sizeof(Value) * fr->nlocals);
    for (size_t i = 0; i < fr->nlocals; i++) fr->locals[i] = VNull();
}

static void pop_frame(VM* vm) {
    Frame* fr = &vm->frames[vm->fp - 1];
    free(fr->locals);
    vm->fp--;
}

/* ===== Main VM loop ===== */

void vm_run_module(VM* vm) {
    vm->next_gc = 1024 * 1024;

    push_frame(vm, &vm->bc.module, false, 0);

    while (vm->fp > 0) {
        Frame* fr = &vm->frames[vm->fp - 1];
        const uint8_t* code = fr->chunk->code;

        Op op = (Op)code[fr->ip++];

        switch (op) {
            case OP_HALT: {
                pop_frame(vm);
            } break;

            case OP_PUSH_INT: {
                int64_t x = read_i64(code, &fr->ip);
                push(vm, VInt(x));
            } break;

            case OP_PUSH_BOOL: {
                uint8_t b = code[fr->ip++];
                push(vm, VBool(b != 0));
            } break;

            case OP_PUSH_NULL:
                push(vm, VNull());
                break;

            case OP_PUSH_STR: {
                uint16_t idx = read_u16(code, &fr->ip);
                const char* s = vm->bc.str_pool[idx];
                ObjStr* o = new_string(vm, s, strlen(s));
                push(vm, VObj((Obj*)o));
                maybe_gc(vm);
            } break;

            case OP_GET_LOCAL: {
                uint16_t idx = read_u16(code, &fr->ip);
                if (idx >= fr->nlocals) { fprintf(stderr, "VMError: bad local index\n"); exit(1); }
                push(vm, fr->locals[idx]);
            } break;

            case OP_SET_LOCAL: {
                uint16_t idx = read_u16(code, &fr->ip);
                if (idx >= fr->nlocals) { fprintf(stderr, "VMError: bad local index\n"); exit(1); }
                Value v = pop(vm);
                typecheck_store(fr->chunk, idx, v);
                fr->locals[idx] = v;
            } break;

            case OP_VOID_LOCAL: {
                (void)read_u16(code, &fr->ip);
            } break;

            case OP_ADD: {
                Value b = pop(vm), a = pop(vm);
                if (a.tag != V_INT || b.tag != V_INT) { fprintf(stderr, "TypeError: + expects int\n"); exit(1); }
                push(vm, VInt(a.as.i + b.as.i));
            } break;

            case OP_SUB: {
                Value b = pop(vm), a = pop(vm);
                if (a.tag != V_INT || b.tag != V_INT) { fprintf(stderr, "TypeError: - expects int\n"); exit(1); }
                push(vm, VInt(a.as.i - b.as.i));
            } break;

            case OP_MUL: {
                Value b = pop(vm), a = pop(vm);
                if (a.tag != V_INT || b.tag != V_INT) { fprintf(stderr, "TypeError: * expects int\n"); exit(1); }
                push(vm, VInt(a.as.i * b.as.i));
            } break;

            case OP_DIV: {
                Value b = pop(vm), a = pop(vm);
                if (a.tag != V_INT || b.tag != V_INT) { fprintf(stderr, "TypeError: / expects int\n"); exit(1); }
                if (b.as.i == 0) { fprintf(stderr, "ZeroDivisionError: division by zero\n"); exit(1); }
                push(vm, VInt(a.as.i / b.as.i));
            } break;
            
            case OP_MOD: {
              Value b = pop(vm), a = pop(vm);
              if (a.tag != V_INT || b.tag != V_INT) { fprintf(stderr, "TypeError: %% expects int\n"); exit(1); }
              if (b.as.i == 0) { fprintf(stderr, "ZeroDivisionError: modulo by zero\n"); exit(1); }
              push(vm, VInt(a.as.i % b.as.i));
            } break;


            case OP_EQEQ: {
                Value b = pop(vm), a = pop(vm);
                if (a.tag != b.tag) { push(vm, VBool(false)); break; }
                if (a.tag == V_INT) push(vm, VBool(a.as.i == b.as.i));
                else if (a.tag == V_BOOL) push(vm, VBool(a.as.b == b.as.b));
                else if (a.tag == V_NULL) push(vm, VBool(true));
                else push(vm, VBool(a.as.obj == b.as.obj));
            } break;

            case OP_NEQ: {
                Value b = pop(vm), a = pop(vm);
                if (a.tag != b.tag) { push(vm, VBool(true)); break; }
                if (a.tag == V_INT) push(vm, VBool(a.as.i != b.as.i));
                else if (a.tag == V_BOOL) push(vm, VBool(a.as.b != b.as.b));
                else if (a.tag == V_NULL) push(vm, VBool(false));
                else push(vm, VBool(a.as.obj != b.as.obj));
            } break;

            case OP_LT:
            case OP_LTE:
            case OP_GT:
            case OP_GTE: {
                Value b = pop(vm), a = pop(vm);
                if (a.tag != V_INT || b.tag != V_INT) { fprintf(stderr, "TypeError: comparison expects int\n"); exit(1); }
                bool r = false;
                if (op == OP_LT)  r = a.as.i <  b.as.i;
                if (op == OP_LTE) r = a.as.i <= b.as.i;
                if (op == OP_GT)  r = a.as.i >  b.as.i;
                if (op == OP_GTE) r = a.as.i >= b.as.i;
                push(vm, VBool(r));
            } break;

            case OP_NOT: {
                Value v = pop(vm);
                if (v.tag != V_BOOL) { fprintf(stderr, "TypeError: NOT expects bool\n"); exit(1); }
                push(vm, VBool(!v.as.b));
            } break;

            case OP_AND:
            case OP_OR:
            case OP_NAND:
            case OP_NOR: {
                Value b = pop(vm), a = pop(vm);
                if (a.tag != V_BOOL || b.tag != V_BOOL) { fprintf(stderr, "TypeError: logical op expects bool\n"); exit(1); }
                bool r=false;
                if (op==OP_AND) r = a.as.b && b.as.b;
                if (op==OP_OR)  r = a.as.b || b.as.b;
                if (op==OP_NAND) r = !(a.as.b && b.as.b);
                if (op==OP_NOR)  r = !(a.as.b || b.as.b);
                push(vm, VBool(r));
            } break;

            case OP_JMP: {
                int32_t rel = read_i32(code, &fr->ip);
                fr->ip = (size_t)((int32_t)fr->ip + rel);
            } break;

            case OP_JMP_IF_FALSE: {
                int32_t rel = read_i32(code, &fr->ip);
                Value v = pop(vm);
                if (v.tag != V_BOOL) { fprintf(stderr, "TypeError: IF/WHILE/FOR condition must be bool in phase 2\n"); exit(1); }
                if (is_falsey(v)) fr->ip = (size_t)((int32_t)fr->ip + rel);
            } break;

            case OP_MAKE_LIST: {
                uint16_t n = read_u16(code, &fr->ip);
                ObjList* L = new_list(vm, n ? n : 1);
                L->len = n;
                for (int i=(int)n-1;i>=0;i--) L->items[i] = pop(vm);
                push(vm, VObj((Obj*)L));
                maybe_gc(vm);
            } break;

            case OP_INDEX_GET: {
                Value idxv = pop(vm);
                Value basev = pop(vm);
                if (idxv.tag != V_INT) { fprintf(stderr, "TypeError: index must be int\n"); exit(1); }
                int64_t idx = idxv.as.i;

                if (!(basev.tag == V_OBJ && basev.as.obj && basev.as.obj->kind == OBJ_LIST)) {
                    fprintf(stderr, "TypeError: indexing requires list\n");
                    exit(1);
                }
                ObjList* L = (ObjList*)basev.as.obj;
                if (idx < 0 || (size_t)idx >= L->len) { fprintf(stderr, "IndexError: index out of range\n"); exit(1); }
                push(vm, L->items[(size_t)idx]);
            } break;

            case OP_INDEX_SET: {
              Value val = pop(vm);
              Value idxv = pop(vm);
              Value basev = pop(vm);
              
              if (idxv.tag != V_INT) { fprintf(stderr, "TypeError: index must be int\n"); exit(1); }
              int64_t idx = idxv.as.i;

              if (!(basev.tag == V_OBJ && basev.as.obj && basev.as.obj->kind == OBJ_LIST)) {
                fprintf(stderr, "TypeError: indexing requires iterable\n");
                exit(1);
              }
              ObjList* L = (ObjList*)basev.as.obj;
              if (idx < 0 || (size_t)idx >= L->len) { fprintf(stderr, "IndexError: index out of range\n"); exit(1); }
              L->items[(size_t)idx] = val;
            } break;

            case OP_CALL: {
                uint16_t fi = read_u16(code, &fr->ip);
                uint16_t argc = read_u16(code, &fr->ip);

                Value* args = (Value*)xmalloc(sizeof(Value) * argc);
                for (int i = (int)argc - 1; i >= 0; i--) args[i] = pop(vm);

                bool has_var = vm->bc.func_has_varargs[fi];
                size_t fixed = vm->bc.func_fixed_param_count[fi];

                if (!has_var) {
                    enforce_param_types_fixed(vm, fi, args, argc);
                    Chunk* fc = &vm->bc.funcs[fi];
                    push_frame(vm, fc, true, fi);
                    Frame* callee = &vm->frames[vm->fp - 1];
                    for (uint16_t i = 0; i < argc; i++) callee->locals[i] = args[i];
                    free(args);
                    break;
                }

                if (argc < fixed) {
                    fprintf(stderr, "TypeError: function '%s' expects at least %zu args but got %u\n",
                        vm->bc.func_names[fi], fixed, (unsigned)argc);
                    exit(1);
                }

                // check fixed params
                if (fixed > 0) {
                    // reuse fixed checker by slicing
                    uint16_t fixed_argc = (uint16_t)fixed;
                    Value* fixed_args = (Value*)xmalloc(sizeof(Value)*fixed_argc);
                    for (size_t i=0;i<fixed;i++) fixed_args[i] = args[i];

                    // temporarily lie about argc in checker by using bc.func_param_count which is fixed already
                    enforce_param_types_fixed(vm, fi, fixed_args, fixed_argc);
                    free(fixed_args);
                }

                // pack varargs into list
                size_t extra = argc - fixed;
                ObjList* L = new_list(vm, extra ? extra : 1);
                L->len = extra;

                TypeSpec vts = vm->bc.func_varargs_ts[fi];
                for (size_t i=0;i<extra;i++) {
                    Value v = args[fixed+i];
                    enforce_one_type(vts, v, "vararg");
                    L->items[i] = v;
                }

                Chunk* fc = &vm->bc.funcs[fi];
                push_frame(vm, fc, true, fi);
                Frame* callee = &vm->frames[vm->fp - 1];

                // fixed args
                for (size_t i=0;i<fixed;i++) callee->locals[i] = args[i];

                // varargs list goes in slot fixed (emitter reserved "...")
                callee->locals[fixed] = VObj((Obj*)L);

                maybe_gc(vm);
                free(args);
            } break;

            case OP_CALL_BUILTIN: {
                uint16_t bid  = read_u16(code, &fr->ip);
                uint16_t argc = read_u16(code, &fr->ip);

                switch ((BuiltinId)bid) {
                    case BI_SAY: {
                        if (argc != 1) { fprintf(stderr, "VMError: SAY argc mismatch\n"); exit(1); }
                        Value v = pop(vm);
                        builtin_say(v);
                        push(vm, VNull());
                    } break;

                    case BI_LENGTH: {
                        if (argc != 1) { fprintf(stderr, "VMError: LENGTH argc mismatch\n"); exit(1); }
                        Value v = pop(vm);
                        push(vm, builtin_length(v));
                    } break;

                    case BI_TYPE: {
                        if (argc != 1) { fprintf(stderr, "VMError: TYPE argc mismatch\n"); exit(1); }
                        Value v = pop(vm);
                        push(vm, builtin_type(vm, v));
                    } break;

                    case BI_RANGE: {
                        if (argc != 1) { fprintf(stderr, "VMError: RANGE argc mismatch\n"); exit(1); }
                        Value v = pop(vm);
                        push(vm, builtin_range(vm, v));
                    } break;

                    case BI_PUSH: {
                        if (argc != 2) { fprintf(stderr, "VMError: PUSH argc mismatch\n"); exit(1); }
                        Value item  = pop(vm);
                        Value listv = pop(vm);
                        push(vm, builtin_push(vm, listv, item));
                    } break;

                    case BI_POP: {
                        if (argc != 1) { fprintf(stderr, "VMError: POP argc mismatch\n"); exit(1); }
                        Value listv = pop(vm);
                        push(vm, builtin_pop(vm, listv));
                    } break;

                    default:
                        fprintf(stderr, "VMError: unknown builtin\n"); exit(1);
                }
            } break; 

            case OP_POP:
                (void)pop(vm);
                break;

            case OP_RET: {
                Value rv = (vm->sp > 0) ? pop(vm) : VNull();

                if (fr->is_function) enforce_return_type(vm, fr->func_index, rv);

                pop_frame(vm);

                if (vm->fp > 0) push(vm, rv);
            } break;

            default:
                fprintf(stderr, "VMError: unknown opcode %d\n", (int)op);
                exit(1);
        }
    }
}