#ifndef LUNA_X86_64_ABI_INTERNAL_H
#define LUNA_X86_64_ABI_INTERNAL_H

#include "luna/backend/x86_64/abi.h"

#include <stdbool.h>

void luna_x86_64_abi_function_destroy_internal(LunaX8664FunctionAbi *abi);
bool luna_x86_64_abi_classify_function_internal(
    const LunaX8664MachineFunction *function, LunaX8664FunctionAbi *abi);

#endif
