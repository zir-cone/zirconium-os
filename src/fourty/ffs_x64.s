.intel_syntax noprefix
.text
.global ffs_memzero_x64
.type ffs_memzero_x64, @function
ffs_memzero_x64:
    test rsi, rsi
    je   .done
    xor  rax, rax
    mov  rcx, rsi
    rep  stosb
.done:
    ret