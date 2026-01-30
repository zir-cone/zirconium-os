#include "../compiler/core.h"

int ccl_run_source(const char* source, size_t len, const char* file_label) {
    if (!source) return 1;

    Source S;
    S.file = file_label ? file_label : "<input>";
    S.src = source;
    S.src_len = len;

    CCL_JmpBuf env;
    int jmp_result = ccl_setjmp(&env);
    if (jmp_result != 0) {
        ccl_clear_abort_env();
        return jmp_result;
    }

    ccl_set_abort_env(&env);

    Lexer L;
    memset(&L, 0, sizeof(L));
    L.S = S;
    lex_all(&L);

    Parser P;
    memset(&P, 0, sizeof(P));
    P.S = S;
    P.toks = L.toks;
    P.ntoks = L.ntoks;
    P.pos = 0;

    Program prog = parse_program(&P);
    Bytecode bc = emit_bytecode(&prog, &S);

    VM vm;
    memset(&vm, 0, sizeof(vm));
    vm.S = &S;
    vm.bc = bc;

    vm_run_module(&vm);
    ccl_clear_abort_env();
    return 0;
}