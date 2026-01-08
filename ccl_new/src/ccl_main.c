#include "core.h"

static char* read_file_all(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "fatal: cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fprintf(stderr, "fatal: ftell failed\n"); exit(1); }
    char* buf = (char*)xmalloc((size_t)sz + 1);
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = 0;
    if (out_len) *out_len = got;
    return buf;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: ccl.exe <file.clam>\n");
        return 1;
    }

    size_t len=0;
    char* src = read_file_all(argv[1], &len);

    Source S = { argv[1], src, len };

    Lexer L;
    memset(&L,0,sizeof(L));
    L.S = S;
    lex_all(&L);

    Parser P;
    memset(&P,0,sizeof(P));
    P.S = S;
    P.toks = L.toks;
    P.ntoks = L.ntoks;
    P.pos = 0;

    Program prog = parse_program(&P);

    Bytecode bc = emit_bytecode(&prog, &S);

    VM vm;
    memset(&vm,0,sizeof(vm));
    vm.S = &S;
    vm.bc = bc;

    vm_run_module(&vm);
    return 0;
}