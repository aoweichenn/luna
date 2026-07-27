#ifndef LUNA_RUNTIME_X86_64_LINUX_SYSCALL_H
#define LUNA_RUNTIME_X86_64_LINUX_SYSCALL_H

#include "luna/frontend/support/buffer.h"
#include "luna/frontend/support/string_view.h"

#include <stdbool.h>
#include <stdio.h>

enum {
    LUNA_X86_64_LINUX_SYSCALL_MAX_ARGUMENT_COUNT = 6,
    LUNA_X86_64_LINUX_SYSCALL_MAX_ERRNO = 4095
};

/*
 * 生成项目自有的 x86-64 Linux 原始系统调用 ABI 目标文件。公开入口接受
 * System V 参数并转换为内核要求的 rax/rdi/rsi/rdx/r10/r8/r9 寄存器。
 */
bool luna_x86_64_linux_syscall_abi_emit_object(FILE *diagnostic_stream,
                                               LunaStringBuilder *output);

/*
 * 按字节验证目标文件是否与当前系统调用 ABI 契约完全一致。该验证用于拒绝
 * 被篡改、版本不匹配或由其他实现冒充的运行时边界。
 */
bool luna_x86_64_linux_syscall_abi_verify_object(LunaStringView object,
                                                 FILE *diagnostic_stream);

#endif
