#include "luna/target/target.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

TEST(TargetTest, ResolvesTheCanonicalBootstrapTarget) {
    const LunaTargetInfo *target = luna_target_info_default();
    ASSERT_NE(target, nullptr);
    EXPECT_STREQ(target->triple, LUNA_TARGET_TRIPLE_X86_64_UNKNOWN_LINUX_GNU);
    EXPECT_EQ(target->architecture, LUNA_TARGET_ARCHITECTURE_X86_64);
    EXPECT_EQ(target->operating_system, LUNA_TARGET_OPERATING_SYSTEM_LINUX);
    EXPECT_EQ(target->abi, LUNA_TARGET_ABI_SYSTEM_V);
    EXPECT_TRUE(luna_target_info_is_supported(target));
    EXPECT_EQ(luna_target_info_from_triple(target->triple), target);
    EXPECT_EQ(luna_target_info_from_triple("aarch64-unknown-linux-gnu"),
              nullptr);
    EXPECT_EQ(luna_target_info_from_triple(nullptr), nullptr);
}

TEST(TargetTest, DescribesTheX8664SystemVDataLayout) {
    const LunaDataLayout &layout = luna_target_info_default()->data_layout;
    ASSERT_TRUE(luna_data_layout_is_valid(&layout));
    EXPECT_EQ(layout.byte_order, LUNA_BYTE_ORDER_LITTLE_ENDIAN);
    EXPECT_EQ(layout.boolean.size_bits, 8U);
    EXPECT_EQ(layout.boolean.abi_alignment_bits, 8U);
    EXPECT_EQ(layout.float32.size_bits, 32U);
    EXPECT_EQ(layout.float32.abi_alignment_bits, 32U);
    EXPECT_EQ(layout.float64.size_bits, 64U);
    EXPECT_EQ(layout.float64.abi_alignment_bits, 64U);
    EXPECT_EQ(layout.pointer.size_bits, 64U);
    EXPECT_EQ(layout.pointer.abi_alignment_bits, 64U);

    constexpr std::array<std::uint32_t, 4U> LUNA_TEST_WIDTHS = {
        8U,
        16U,
        32U,
        64U,
    };
    for (const std::uint32_t width : LUNA_TEST_WIDTHS) {
        const LunaScalarLayout *integer =
            luna_data_layout_integer(&layout, width);
        ASSERT_NE(integer, nullptr);
        EXPECT_EQ(integer->size_bits, width);
        EXPECT_EQ(integer->abi_alignment_bits, width);
    }
    EXPECT_EQ(luna_data_layout_integer(&layout, 24U), nullptr);
    EXPECT_EQ(luna_data_layout_integer(nullptr, 32U), nullptr);
    EXPECT_EQ(luna_data_layout_float(&layout, 32U), &layout.float32);
    EXPECT_EQ(luna_data_layout_float(&layout, 64U), &layout.float64);
    EXPECT_EQ(luna_data_layout_float(&layout, 16U), nullptr);
    EXPECT_EQ(luna_data_layout_float(nullptr, 32U), nullptr);
}

TEST(TargetTest, RejectsMalformedLayoutsAndUnsupportedTargets) {
    const LunaTargetInfo *canonical = luna_target_info_default();
    LunaTargetInfo target = *canonical;
    target.data_layout.pointer.size_bits = 48U;
    EXPECT_FALSE(luna_data_layout_is_valid(&target.data_layout));
    EXPECT_FALSE(luna_target_info_is_supported(&target));

    target = *canonical;
    target.data_layout.integer32.abi_alignment_bits = 24U;
    EXPECT_FALSE(luna_data_layout_is_valid(&target.data_layout));
    EXPECT_FALSE(
        luna_data_layout_equal(&target.data_layout, &canonical->data_layout));

    target = *canonical;
    target.data_layout.float64.size_bits = 32U;
    EXPECT_FALSE(luna_data_layout_is_valid(&target.data_layout));
    EXPECT_FALSE(
        luna_data_layout_equal(&target.data_layout, &canonical->data_layout));

    EXPECT_FALSE(luna_data_layout_is_valid(nullptr));
    EXPECT_FALSE(luna_data_layout_equal(nullptr, &canonical->data_layout));
    EXPECT_FALSE(luna_target_info_is_supported(nullptr));
}
