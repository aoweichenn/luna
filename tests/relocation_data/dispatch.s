    .section .rodata,"a",@progbits
    .balign 8
    .type dispatch_table, @object
dispatch_table:
    .quad dispatch_target
    .size dispatch_table, .-dispatch_table

    .text
    .globl _start
    .type _start, @function
_start:
    movq dispatch_table(%rip), %rax
    call *%rax
    movl %eax, %edi
    movl $60, %eax
    syscall
    .size _start, .-_start

    .type dispatch_target, @function
dispatch_target:
    movl $42, %eax
    ret
    .size dispatch_target, .-dispatch_target

    .section .note.GNU-stack,"",@progbits
