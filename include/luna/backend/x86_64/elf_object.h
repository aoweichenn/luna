#ifndef LUNA_X86_64_ELF_OBJECT_H
#define LUNA_X86_64_ELF_OBJECT_H

#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/frontend/support/buffer.h"
#include "luna/frontend/support/string_view.h"

#include <stdbool.h>
#include <stdio.h>

/*
 * 将 Luna x86-64 后端输出的封闭汇编方言编码为 ELF64 可重定位目标文件。
 * 该接口不提供通用 GNU 汇编器能力。
 */
bool luna_x86_64_assemble_elf_object(LunaStringView assembly,
                                     LunaDiagnosticEngine *diagnostics,
                                     LunaStringBuilder *output);

/*
 * 对 Luna 生成的 ELF64 可重定位目标文件执行结构校验。
 * 校验器不信任输入中的偏移、数量、名称和交叉引用。
 */
bool luna_x86_64_elf_object_verify(LunaStringView object,
                                   FILE *diagnostic_stream);

#endif
