#ifndef LUNA_TARGET_TARGET_H
#define LUNA_TARGET_TARGET_H

#include <stdbool.h>
#include <stdint.h>

#define LUNA_TARGET_TRIPLE_X86_64_UNKNOWN_LINUX_GNU "x86_64-unknown-linux-gnu"

typedef enum LunaByteOrder {
    LUNA_BYTE_ORDER_INVALID,
    LUNA_BYTE_ORDER_LITTLE_ENDIAN,
    LUNA_BYTE_ORDER_BIG_ENDIAN
} LunaByteOrder;

typedef struct LunaScalarLayout {
    uint32_t size_bits;
    uint32_t abi_alignment_bits;
} LunaScalarLayout;

typedef struct LunaDataLayout {
    LunaByteOrder byte_order;
    LunaScalarLayout boolean;
    LunaScalarLayout integer8;
    LunaScalarLayout integer16;
    LunaScalarLayout integer32;
    LunaScalarLayout integer64;
    LunaScalarLayout float32;
    LunaScalarLayout float64;
    LunaScalarLayout pointer;
} LunaDataLayout;

typedef enum LunaTargetArchitecture {
    LUNA_TARGET_ARCHITECTURE_UNKNOWN,
    LUNA_TARGET_ARCHITECTURE_X86_64
} LunaTargetArchitecture;

typedef enum LunaTargetOperatingSystem {
    LUNA_TARGET_OPERATING_SYSTEM_UNKNOWN,
    LUNA_TARGET_OPERATING_SYSTEM_LINUX
} LunaTargetOperatingSystem;

typedef enum LunaTargetAbi {
    LUNA_TARGET_ABI_UNKNOWN,
    LUNA_TARGET_ABI_SYSTEM_V
} LunaTargetAbi;

typedef struct LunaTargetInfo {
    const char *triple;
    LunaTargetArchitecture architecture;
    LunaTargetOperatingSystem operating_system;
    LunaTargetAbi abi;
    LunaDataLayout data_layout;
} LunaTargetInfo;

#ifdef __cplusplus
extern "C" {
#endif

const LunaTargetInfo *luna_target_info_default(void);
const LunaTargetInfo *luna_target_info_from_triple(const char *triple);
bool luna_target_info_is_supported(const LunaTargetInfo *target);

bool luna_data_layout_is_valid(const LunaDataLayout *layout);
bool luna_data_layout_equal(const LunaDataLayout *left,
                            const LunaDataLayout *right);
const LunaScalarLayout *luna_data_layout_integer(const LunaDataLayout *layout,
                                                 uint32_t bit_width);
const LunaScalarLayout *luna_data_layout_float(const LunaDataLayout *layout,
                                               uint32_t bit_width);

#ifdef __cplusplus
}
#endif

#endif
