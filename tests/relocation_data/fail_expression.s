    .section .rodata,"a",@progbits
    .extern dispatch_target
    .quad dispatch_target+8
