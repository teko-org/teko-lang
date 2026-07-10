.intel_syntax noprefix
.text
.globl f
f:
    push rbp
    mov rbp, rsp
    sub rsp, 8
    push rbx
    push r12
    push r13
    pop r13
    pop r12
    pop rbx
    leave
g:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    pop r12
    pop rbx
    leave
