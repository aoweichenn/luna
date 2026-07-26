#include "luna_c_api.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>

namespace luna::test {

TEST(SourceTest, CopiesExactByteRangeAndAppendsSentinel) {
    constexpr std::array<char, 3U> LUNA_TEST_BYTES = {'a', '\0', 'b'};
    LunaSourceFile source{};

    ASSERT_TRUE(luna_source_from_bytes("<bytes>", LUNA_TEST_BYTES.data(),
                                       LUNA_TEST_BYTES.size(), &source));
    ASSERT_EQ(source.length, LUNA_TEST_BYTES.size());
    EXPECT_EQ(source.text[0], 'a');
    EXPECT_EQ(source.text[1], '\0');
    EXPECT_EQ(source.text[2], 'b');
    EXPECT_EQ(source.text[3], '\0');

    const LunaSourceSpan span{
        .source = &source,
        .offset = 1U,
        .length = 2U,
        .line = 1U,
        .column = 2U,
    };
    const LunaStringView view = luna_source_span_text(span);
    ASSERT_EQ(view.length, 2U);
    EXPECT_EQ(std::string_view(view.data, view.length),
              std::string_view(LUNA_TEST_BYTES.data() + 1U, 2U));

    luna_source_destroy(&source);
}

TEST(SourceTest, RejectsNullBytesAndOutOfRangeSpan) {
    LunaSourceFile source{};
    EXPECT_FALSE(luna_source_from_bytes("<null>", nullptr, 0U, &source));
    EXPECT_FALSE(luna_source_from_bytes(nullptr, "", 0U, &source));
    EXPECT_FALSE(luna_source_from_memory("<null>", nullptr, &source));
    EXPECT_FALSE(luna_source_load(nullptr, &source));
    EXPECT_FALSE(luna_source_from_memory("<null-output>", "", nullptr));

    ASSERT_TRUE(luna_source_from_memory("<memory>", "abc", &source));
    const LunaSourceSpan span{
        .source = &source,
        .offset = 2U,
        .length = 2U,
        .line = 1U,
        .column = 3U,
    };
    const LunaStringView view = luna_source_span_text(span);
    EXPECT_EQ(view.data, nullptr);
    EXPECT_EQ(view.length, 0U);
    luna_source_destroy(&source);
}

}
