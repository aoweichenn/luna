    .section .rodata,"a",@progbits
    .balign 8
    .type .Ldata, @object
.Ldata:
    .byte 0x41
    .size .Ldata, .-.Ldata

    .extern external_value
    .text
    .globl _start
    .type _start, @function
_start:
    xorl %ebp, %ebp
    leaq .Ldata(%rip), %r12
    movzbl (%r12), %edi
    call external_value
    movabsq $0x1122334455667788, %r13
    movq %r13, -8(%rbp)
    movss %xmm9, 8(%r11)
    cvttsd2siq %xmm0, %rax
    shrq $8, %r10
    rep movsb
    jne 1f
    ud2
1:
    syscall
    ret
    .size _start, .-_start
    .section .note.GNU-stack,"",@progbits
