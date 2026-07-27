#include "luna/runtime/x86_64/linux_syscall.h"

#include "luna/backend/x86_64/elf_object.h"
#include "luna/frontend/diagnostic/diagnostic.h"

#include <stdarg.h>
#include <string.h>

/*
 * 这些包装器是 System V ABI 与 Linux x86-64 系统调用 ABI 之间的唯一
 * 汇编边界。第七个 System V 参数位于返回地址之后的栈槽中。
 */
static const char luna_linux_syscall_abi_assembly[] =
    "    .text\n"
    "    .balign 16\n"
    "    .globl luna_linux_syscall0\n"
    "    .type luna_linux_syscall0, @function\n"
    "luna_linux_syscall0:\n"
    "    movq %rdi, %rax\n"
    "    syscall\n"
    "    ret\n"
    "    .size luna_linux_syscall0, .-luna_linux_syscall0\n"
    "\n"
    "    .globl luna_linux_syscall1\n"
    "    .type luna_linux_syscall1, @function\n"
    "luna_linux_syscall1:\n"
    "    movq %rdi, %rax\n"
    "    movq %rsi, %rdi\n"
    "    syscall\n"
    "    ret\n"
    "    .size luna_linux_syscall1, .-luna_linux_syscall1\n"
    "\n"
    "    .globl luna_linux_syscall2\n"
    "    .type luna_linux_syscall2, @function\n"
    "luna_linux_syscall2:\n"
    "    movq %rdi, %rax\n"
    "    movq %rsi, %rdi\n"
    "    movq %rdx, %rsi\n"
    "    syscall\n"
    "    ret\n"
    "    .size luna_linux_syscall2, .-luna_linux_syscall2\n"
    "\n"
    "    .globl luna_linux_syscall3\n"
    "    .type luna_linux_syscall3, @function\n"
    "luna_linux_syscall3:\n"
    "    movq %rdi, %rax\n"
    "    movq %rsi, %rdi\n"
    "    movq %rdx, %rsi\n"
    "    movq %rcx, %rdx\n"
    "    syscall\n"
    "    ret\n"
    "    .size luna_linux_syscall3, .-luna_linux_syscall3\n"
    "\n"
    "    .globl luna_linux_syscall4\n"
    "    .type luna_linux_syscall4, @function\n"
    "luna_linux_syscall4:\n"
    "    movq %rdi, %rax\n"
    "    movq %rsi, %rdi\n"
    "    movq %rdx, %rsi\n"
    "    movq %rcx, %rdx\n"
    "    movq %r8, %r10\n"
    "    syscall\n"
    "    ret\n"
    "    .size luna_linux_syscall4, .-luna_linux_syscall4\n"
    "\n"
    "    .globl luna_linux_syscall5\n"
    "    .type luna_linux_syscall5, @function\n"
    "luna_linux_syscall5:\n"
    "    movq %rdi, %rax\n"
    "    movq %rsi, %rdi\n"
    "    movq %rdx, %rsi\n"
    "    movq %rcx, %rdx\n"
    "    movq %r8, %r10\n"
    "    movq %r9, %r8\n"
    "    syscall\n"
    "    ret\n"
    "    .size luna_linux_syscall5, .-luna_linux_syscall5\n"
    "\n"
    "    .globl luna_linux_syscall6\n"
    "    .type luna_linux_syscall6, @function\n"
    "luna_linux_syscall6:\n"
    "    movq %rdi, %rax\n"
    "    movq %rsi, %rdi\n"
    "    movq %rdx, %rsi\n"
    "    movq %rcx, %rdx\n"
    "    movq %r8, %r10\n"
    "    movq %r9, %r8\n"
    "    movq 8(%rsp), %r9\n"
    "    syscall\n"
    "    ret\n"
    "    .size luna_linux_syscall6, .-luna_linux_syscall6\n"
    "\n"
    "    .section .note.GNU-stack,\"\",@progbits\n";

static bool luna_linux_syscall_error(FILE *stream, const char *format, ...)
    LUNA_PRINTF_LIKE(2, 3);

static bool luna_linux_syscall_error(FILE *stream, const char *format, ...) {
    if (stream != NULL) {
        va_list arguments;
        va_start(arguments, format);
        (void)fputs("Linux syscall ABI verification error: ", stream);
        (void)vfprintf(stream, format, arguments);
        (void)fputc('\n', stream);
        va_end(arguments);
    }
    return false;
}

static bool luna_linux_syscall_build_object(FILE *diagnostic_stream,
                                            LunaStringBuilder *output) {
    if (output == NULL || output->length != 0U) {
        return false;
    }
    LunaDiagnosticEngine diagnostics;
    luna_diagnostic_init(&diagnostics, diagnostic_stream);
    return luna_x86_64_assemble_elf_object(
        (LunaStringView){
            .data = luna_linux_syscall_abi_assembly,
            .length = sizeof(luna_linux_syscall_abi_assembly) - 1U,
        },
        &diagnostics, output);
}

bool luna_x86_64_linux_syscall_abi_emit_object(FILE *diagnostic_stream,
                                               LunaStringBuilder *output) {
    if (output == NULL || output->length != 0U) {
        if (output != NULL) {
            output->length = 0U;
            if (output->data != NULL) {
                output->data[0] = '\0';
            }
        }
        return luna_linux_syscall_error(diagnostic_stream,
                                        "invalid object output state");
    }
    if (!luna_linux_syscall_build_object(diagnostic_stream, output)) {
        output->length = 0U;
        if (output->data != NULL) {
            output->data[0] = '\0';
        }
        return luna_linux_syscall_error(
            diagnostic_stream, "failed to encode the canonical ABI object");
    }
    return true;
}

bool luna_x86_64_linux_syscall_abi_verify_object(LunaStringView object,
                                                 FILE *diagnostic_stream) {
    if (object.data == NULL || object.length == 0U) {
        return luna_linux_syscall_error(diagnostic_stream,
                                        "missing ABI object bytes");
    }

    LunaStringBuilder canonical;
    luna_string_builder_init(&canonical);
    const bool built =
        luna_linux_syscall_build_object(diagnostic_stream, &canonical);
    const bool matches = built && canonical.length == object.length &&
                         memcmp(luna_string_builder_data(&canonical),
                                object.data, object.length) == 0;
    luna_string_builder_destroy(&canonical);
    return matches ||
           luna_linux_syscall_error(
               diagnostic_stream,
               "object bytes do not match the canonical ABI contract");
}
