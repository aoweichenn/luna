#ifndef LUNA_BACKEND_DEBUG_DEBUG_IR_H
#define LUNA_BACKEND_DEBUG_DEBUG_IR_H

#include "luna/frontend/support/buffer.h"
#include "luna/frontend/support/string_view.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum {
    LUNA_DEBUG_IR_VERSION = 1,
    LUNA_DEBUG_IR_MAX_RECORD_COUNT = 1024 * 1024,
    LUNA_DEBUG_IR_MAX_STRING_BYTES = 64 * 1024 * 1024,
    LUNA_DEBUG_IR_MAX_NAME_BYTES = 4096
};

typedef struct LunaDebugIrFile {
    uint32_t path_offset;
    uint32_t path_length;
} LunaDebugIrFile;

typedef struct LunaDebugIrLocation {
    uint64_t code_offset;
    uint32_t file_id;
    uint32_t line;
    uint32_t column;
    bool is_statement;
} LunaDebugIrLocation;

typedef struct LunaDebugIrFunction {
    uint64_t code_begin;
    uint64_t code_end;
    uint32_t name_offset;
    uint32_t name_length;
    uint32_t linkage_name_offset;
    uint32_t linkage_name_length;
    bool is_external;
} LunaDebugIrFunction;

/*
 * Debug IR 是源码语义与最终机器地址之间的稳定边界。code_offset 始终相对
 * 于当前目标文件的代码节；链接器完成布局后才将其转换为绝对地址。
 */
typedef struct LunaDebugIr {
    LunaStringBuilder strings;
    LunaVector files;
    LunaVector locations;
    LunaVector functions;
} LunaDebugIr;

void luna_debug_ir_init(LunaDebugIr *debug_ir);
void luna_debug_ir_destroy(LunaDebugIr *debug_ir);

bool luna_debug_ir_add_file(LunaDebugIr *debug_ir, LunaStringView path,
                            uint32_t *file_id);
bool luna_debug_ir_add_location(LunaDebugIr *debug_ir, uint64_t code_offset,
                                uint32_t file_id, uint32_t line,
                                uint32_t column, bool is_statement);
bool luna_debug_ir_add_function(LunaDebugIr *debug_ir, uint64_t code_begin,
                                uint64_t code_end, LunaStringView name,
                                LunaStringView linkage_name, bool is_external);

bool luna_debug_ir_file_path(const LunaDebugIr *debug_ir, uint32_t file_id,
                             LunaStringView *path);
bool luna_debug_ir_function_name(const LunaDebugIr *debug_ir,
                                 const LunaDebugIrFunction *function,
                                 LunaStringView *name);
bool luna_debug_ir_function_linkage_name(const LunaDebugIr *debug_ir,
                                         const LunaDebugIrFunction *function,
                                         LunaStringView *name);

bool luna_debug_ir_verify(const LunaDebugIr *debug_ir, uint64_t code_size,
                          FILE *diagnostic_stream);
bool luna_debug_ir_encode(const LunaDebugIr *debug_ir,
                          LunaStringBuilder *output);
bool luna_debug_ir_decode(LunaStringView encoded, LunaDebugIr *debug_ir,
                          FILE *diagnostic_stream);

#endif
