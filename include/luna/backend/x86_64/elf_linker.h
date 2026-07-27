#ifndef LUNA_X86_64_ELF_LINKER_H
#define LUNA_X86_64_ELF_LINKER_H

#include "luna/frontend/support/buffer.h"
#include "luna/frontend/support/string_view.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum {
    LUNA_X86_64_ELF_LINK_MAX_INPUT_COUNT = 65535,
    LUNA_X86_64_ELF_LINK_MAX_OBJECT_SIZE = 256 * 1024 * 1024,
    LUNA_X86_64_ELF_LINK_MAX_NAME_LENGTH = 4096
};

typedef struct LunaX8664ElfLinkInput {
    LunaStringView name;
    LunaStringView object;
} LunaX8664ElfLinkInput;

/*
 * 将一组 x86-64 ELF64 可重定位目标文件静态链接为无解释器的 Linux
 * 可执行文件。当前契约只接受受支持的非 PIC 静态重定位。
 */
bool luna_x86_64_link_elf_executable(const LunaX8664ElfLinkInput *inputs,
                                     uint32_t input_count,
                                     LunaStringView entry_symbol,
                                     FILE *diagnostic_stream,
                                     LunaStringBuilder *output);

/*
 * 校验项目链接器生成的静态 ELF64 可执行文件。校验器不信任任何偏移、
 * 长度、数量或段交叉引用。
 */
bool luna_x86_64_elf_executable_verify(LunaStringView executable,
                                       FILE *diagnostic_stream);

#endif
