#include "luna/target/target.h"

#include <stddef.h>
#include <string.h>

static const LunaTargetInfo luna_x86_64_unknown_linux_gnu = {
    .triple = LUNA_TARGET_TRIPLE_X86_64_UNKNOWN_LINUX_GNU,
    .architecture = LUNA_TARGET_ARCHITECTURE_X86_64,
    .operating_system = LUNA_TARGET_OPERATING_SYSTEM_LINUX,
    .abi = LUNA_TARGET_ABI_SYSTEM_V,
    .data_layout =
        {
            .byte_order = LUNA_BYTE_ORDER_LITTLE_ENDIAN,
            .boolean = {.size_bits = 8U, .abi_alignment_bits = 8U},
            .integer8 = {.size_bits = 8U, .abi_alignment_bits = 8U},
            .integer16 = {.size_bits = 16U, .abi_alignment_bits = 16U},
            .integer32 = {.size_bits = 32U, .abi_alignment_bits = 32U},
            .integer64 = {.size_bits = 64U, .abi_alignment_bits = 64U},
            .pointer = {.size_bits = 64U, .abi_alignment_bits = 64U},
        },
};

static bool luna_scalar_layout_is_valid(const LunaScalarLayout *layout,
                                        uint32_t expected_size_bits) {
    if (layout->size_bits != expected_size_bits ||
        layout->abi_alignment_bits == 0U ||
        layout->abi_alignment_bits % 8U != 0U) {
        return false;
    }

    const uint32_t alignment = layout->abi_alignment_bits;
    return (alignment & (alignment - 1U)) == 0U;
}

const LunaTargetInfo *luna_target_info_default(void) {
    return &luna_x86_64_unknown_linux_gnu;
}

const LunaTargetInfo *luna_target_info_from_triple(const char *triple) {
    if (triple != NULL &&
        strcmp(triple, LUNA_TARGET_TRIPLE_X86_64_UNKNOWN_LINUX_GNU) == 0) {
        return &luna_x86_64_unknown_linux_gnu;
    }
    return NULL;
}

bool luna_target_info_is_supported(const LunaTargetInfo *target) {
    return target != NULL && target->triple != NULL &&
           strcmp(target->triple,
                  LUNA_TARGET_TRIPLE_X86_64_UNKNOWN_LINUX_GNU) == 0 &&
           target->architecture == LUNA_TARGET_ARCHITECTURE_X86_64 &&
           target->operating_system == LUNA_TARGET_OPERATING_SYSTEM_LINUX &&
           target->abi == LUNA_TARGET_ABI_SYSTEM_V &&
           luna_data_layout_equal(&target->data_layout,
                                  &luna_x86_64_unknown_linux_gnu.data_layout);
}

bool luna_data_layout_is_valid(const LunaDataLayout *layout) {
    if (layout == NULL ||
        (layout->byte_order != LUNA_BYTE_ORDER_LITTLE_ENDIAN &&
         layout->byte_order != LUNA_BYTE_ORDER_BIG_ENDIAN) ||
        !luna_scalar_layout_is_valid(&layout->boolean, 8U) ||
        !luna_scalar_layout_is_valid(&layout->integer8, 8U) ||
        !luna_scalar_layout_is_valid(&layout->integer16, 16U) ||
        !luna_scalar_layout_is_valid(&layout->integer32, 32U) ||
        !luna_scalar_layout_is_valid(&layout->integer64, 64U)) {
        return false;
    }

    const uint32_t pointer_size = layout->pointer.size_bits;
    if ((pointer_size != 16U && pointer_size != 32U && pointer_size != 64U) ||
        layout->pointer.abi_alignment_bits == 0U ||
        layout->pointer.abi_alignment_bits % 8U != 0U) {
        return false;
    }

    const uint32_t pointer_alignment = layout->pointer.abi_alignment_bits;
    return (pointer_alignment & (pointer_alignment - 1U)) == 0U;
}

bool luna_data_layout_equal(const LunaDataLayout *left,
                            const LunaDataLayout *right) {
    return left != NULL && right != NULL &&
           left->byte_order == right->byte_order &&
           left->boolean.size_bits == right->boolean.size_bits &&
           left->boolean.abi_alignment_bits ==
               right->boolean.abi_alignment_bits &&
           left->integer8.size_bits == right->integer8.size_bits &&
           left->integer8.abi_alignment_bits ==
               right->integer8.abi_alignment_bits &&
           left->integer16.size_bits == right->integer16.size_bits &&
           left->integer16.abi_alignment_bits ==
               right->integer16.abi_alignment_bits &&
           left->integer32.size_bits == right->integer32.size_bits &&
           left->integer32.abi_alignment_bits ==
               right->integer32.abi_alignment_bits &&
           left->integer64.size_bits == right->integer64.size_bits &&
           left->integer64.abi_alignment_bits ==
               right->integer64.abi_alignment_bits &&
           left->pointer.size_bits == right->pointer.size_bits &&
           left->pointer.abi_alignment_bits ==
               right->pointer.abi_alignment_bits;
}

const LunaScalarLayout *luna_data_layout_integer(const LunaDataLayout *layout,
                                                 uint32_t bit_width) {
    if (layout == NULL) {
        return NULL;
    }

    switch (bit_width) {
    case 8U:
        return &layout->integer8;
    case 16U:
        return &layout->integer16;
    case 32U:
        return &layout->integer32;
    case 64U:
        return &layout->integer64;
    default:
        return NULL;
    }
}
