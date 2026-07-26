#include "luna_c_api.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace luna::test {

TEST(VectorTest, PreservesElementsWhileGrowing) {
    LunaVector vector{};
    luna_vector_init(&vector, sizeof(std::uint32_t));

    for (std::uint32_t index = 0U; index < 1024U; index += 1U) {
        ASSERT_TRUE(luna_vector_push(&vector, &index));
    }

    ASSERT_EQ(vector.length, 1024U);
    for (std::size_t index = 0U; index < vector.length; index += 1U) {
        const auto *value = static_cast<const std::uint32_t *>(
            luna_vector_at_const(&vector, index));
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(*value, static_cast<std::uint32_t>(index));
    }
    EXPECT_EQ(luna_vector_at(&vector, vector.length), nullptr);

    luna_vector_destroy(&vector);
}

TEST(VectorTest, RejectsZeroSizedElements) {
    LunaVector vector{};
    luna_vector_init(&vector, 0U);
    const std::uint32_t value = 7U;

    EXPECT_FALSE(luna_vector_push(&vector, &value));
    EXPECT_FALSE(luna_vector_reserve(&vector, 1U));

    luna_vector_destroy(&vector);
}

TEST(StringBuilderTest, SupportsEmbeddedNullAndFormatting) {
    LunaStringBuilder builder{};
    luna_string_builder_init(&builder);
    constexpr std::array<char, 3U> LUNA_TEST_BYTES = {'a', '\0', 'b'};

    ASSERT_TRUE(luna_string_builder_append(&builder, LUNA_TEST_BYTES.data(),
                                           LUNA_TEST_BYTES.size()));
    ASSERT_TRUE(luna_string_builder_append_format(&builder, ":%d", 42));

    const std::string actual{luna_string_builder_data(&builder),
                             builder.length};
    const std::string expected{"a\0b:42", 6U};
    EXPECT_EQ(actual, expected);
    EXPECT_EQ(luna_string_builder_data(&builder)[builder.length], '\0');

    luna_string_builder_destroy(&builder);
}

}
