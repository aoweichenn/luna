#include "luna_c_api.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace luna::test {

TEST(ArenaTest, AllocatesAlignedZeroedMemoryAcrossBlocks) {
    LunaArena arena{};
    luna_arena_init(&arena, 64U);

    auto *integer = static_cast<std::uint32_t *>(luna_arena_allocate_zero(
        &arena, sizeof(std::uint32_t), alignof(std::uint32_t)));
    ASSERT_NE(integer, nullptr);
    EXPECT_EQ(*integer, 0U);
    EXPECT_EQ(
        reinterpret_cast<std::uintptr_t>(integer) % alignof(std::uint32_t), 0U);

    for (std::uint64_t index = 0U; index < 1000U; index += 1U) {
        auto *value = static_cast<std::uint64_t *>(luna_arena_allocate(
            &arena, sizeof(std::uint64_t), alignof(std::uint64_t)));
        ASSERT_NE(value, nullptr);
        *value = index;
        EXPECT_EQ(*value, index);
    }

    char *copy =
        luna_arena_copy_string(&arena, luna_string_view_from_c_string("luna"));
    ASSERT_NE(copy, nullptr);
    EXPECT_STREQ(copy, "luna");

    luna_arena_destroy(&arena);
}

TEST(ArenaTest, RejectsInvalidAlignmentAndOverflow) {
    LunaArena arena{};
    luna_arena_init(&arena, 64U);

    EXPECT_EQ(luna_arena_allocate(&arena, 8U, 3U), nullptr);
    EXPECT_EQ(luna_arena_allocate(&arena, SIZE_MAX, alignof(std::uint64_t)),
              nullptr);

    luna_arena_destroy(&arena);
}

}
