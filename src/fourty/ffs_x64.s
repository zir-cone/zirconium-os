.intel_syntax noprefix
.text
.global ffs_memzero_x64
.type ffs_memzero_x64, @function
ffs_memzero_x64:
    test rsi, rsi
<<<<<<< HEAD
    je   .done
    xor  rax, rax
    mov  rcx, rsi
    rep  stosb
.done:
    ret
=======
    je .done
    xor rax, rax
    mov rcx, rsi
    rep stosb
.done:
    ret
>>>>>>> 6f01370f08b307819c9bd57b453eedf4d2977a6e
