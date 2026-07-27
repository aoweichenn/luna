#include "test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace luna::test {
namespace {

class DebugIrOwner final {
  public:
    explicit DebugIrOwner(bool initialize = true) {
        if (initialize) {
            luna_debug_ir_init(&debug_ir_);
        }
    }
    ~DebugIrOwner() {
        luna_debug_ir_destroy(&debug_ir_);
    }

    DebugIrOwner(const DebugIrOwner &) = delete;
    DebugIrOwner &operator=(const DebugIrOwner &) = delete;
    DebugIrOwner(DebugIrOwner &&) = delete;
    DebugIrOwner &operator=(DebugIrOwner &&) = delete;

    [[nodiscard]] LunaDebugIr *Get() noexcept {
        return &debug_ir_;
    }
    [[nodiscard]] const LunaDebugIr *Get() const noexcept {
        return &debug_ir_;
    }

  private:
    LunaDebugIr debug_ir_{};
};

class StringBuilderOwner final {
  public:
    StringBuilderOwner() {
        luna_string_builder_init(&builder_);
    }
    ~StringBuilderOwner() {
        luna_string_builder_destroy(&builder_);
    }

    StringBuilderOwner(const StringBuilderOwner &) = delete;
    StringBuilderOwner &operator=(const StringBuilderOwner &) = delete;
    StringBuilderOwner(StringBuilderOwner &&) = delete;
    StringBuilderOwner &operator=(StringBuilderOwner &&) = delete;

    [[nodiscard]] LunaStringBuilder *Get() noexcept {
        return &builder_;
    }
    [[nodiscard]] std::string Bytes() const {
        return {luna_string_builder_data(&builder_), builder_.length};
    }

  private:
    LunaStringBuilder builder_{};
};

[[nodiscard]] LunaStringView View(std::string_view value) {
    return LunaStringView{
        .data = value.data(),
        .length = value.size(),
    };
}

TEST(DebugIrTest, RoundTripsVersionedFilesLocationsAndFunctions) {
    DebugIrOwner source;
    std::uint32_t first_file = 0U;
    std::uint32_t second_file = 0U;
    ASSERT_TRUE(luna_debug_ir_add_file(source.Get(), View("src/main.luna"),
                                       &first_file));
    ASSERT_TRUE(luna_debug_ir_add_file(source.Get(), View("src/math.luna"),
                                       &second_file));
    EXPECT_EQ(first_file, 1U);
    EXPECT_EQ(second_file, 2U);

    ASSERT_TRUE(
        luna_debug_ir_add_location(source.Get(), 8U, first_file, 4U, 5U, true));
    ASSERT_TRUE(luna_debug_ir_add_location(source.Get(), 16U, second_file, 9U,
                                           12U, true));
    ASSERT_TRUE(luna_debug_ir_add_function(source.Get(), 4U, 24U, View("main"),
                                           View("_L74657374_6d61696e"), true));
    ASSERT_TRUE(luna_debug_ir_verify(source.Get(), 32U, nullptr));

    StringBuilderOwner encoded;
    ASSERT_TRUE(luna_debug_ir_encode(source.Get(), encoded.Get()));
    const std::string bytes = encoded.Bytes();
    ASSERT_GT(bytes.size(), 52U);

    DebugIrOwner decoded{false};
    ASSERT_TRUE(luna_debug_ir_decode(View(bytes), decoded.Get(), nullptr));
    ASSERT_TRUE(luna_debug_ir_verify(decoded.Get(), 32U, nullptr));
    ASSERT_EQ(decoded.Get()->files.length, 2U);
    ASSERT_EQ(decoded.Get()->locations.length, 2U);
    ASSERT_EQ(decoded.Get()->functions.length, 1U);

    LunaStringView path{};
    ASSERT_TRUE(luna_debug_ir_file_path(decoded.Get(), second_file, &path));
    EXPECT_EQ(std::string_view(path.data, path.length), "src/math.luna");
    const LunaDebugIrFunction *function =
        static_cast<const LunaDebugIrFunction *>(
            luna_vector_at_const(&decoded.Get()->functions, 0U));
    ASSERT_NE(function, nullptr);
    LunaStringView name{};
    LunaStringView linkage_name{};
    ASSERT_TRUE(luna_debug_ir_function_name(decoded.Get(), function, &name));
    ASSERT_TRUE(luna_debug_ir_function_linkage_name(decoded.Get(), function,
                                                    &linkage_name));
    EXPECT_EQ(std::string_view(name.data, name.length), "main");
    EXPECT_EQ(std::string_view(linkage_name.data, linkage_name.length),
              "_L74657374_6d61696e");
}

TEST(DebugIrTest, RejectsOverlapsBoundsAndCorruptedEncoding) {
    DebugIrOwner debug_ir;
    std::uint32_t file_id = 0U;
    ASSERT_TRUE(
        luna_debug_ir_add_file(debug_ir.Get(), View("bounds.luna"), &file_id));
    ASSERT_TRUE(
        luna_debug_ir_add_location(debug_ir.Get(), 12U, file_id, 3U, 7U, true));
    ASSERT_TRUE(luna_debug_ir_add_function(debug_ir.Get(), 8U, 20U,
                                           View("bounded"),
                                           View("_L_626f756e646564"), false));
    EXPECT_FALSE(luna_debug_ir_add_function(debug_ir.Get(), 16U, 24U,
                                            View("overlap"),
                                            View("_L_6f7665726c6170"), false));
    EXPECT_FALSE(luna_debug_ir_verify(debug_ir.Get(), 16U, nullptr));
    EXPECT_TRUE(luna_debug_ir_verify(debug_ir.Get(), 24U, nullptr));

    StringBuilderOwner encoded;
    ASSERT_TRUE(luna_debug_ir_encode(debug_ir.Get(), encoded.Get()));
    std::string corrupted = encoded.Bytes();
    corrupted[0U] = 'X';
    DebugIrOwner decoded{false};
    EXPECT_FALSE(luna_debug_ir_decode(View(corrupted), decoded.Get(), nullptr));

    DebugIrOwner uncovered_location;
    std::uint32_t uncovered_file_id = 0U;
    ASSERT_TRUE(luna_debug_ir_add_file(
        uncovered_location.Get(), View("uncovered.luna"), &uncovered_file_id));
    ASSERT_TRUE(luna_debug_ir_add_location(uncovered_location.Get(), 12U,
                                           uncovered_file_id, 3U, 7U, true));
    ASSERT_TRUE(luna_debug_ir_add_location(uncovered_location.Get(), 22U,
                                           uncovered_file_id, 4U, 1U, true));
    ASSERT_TRUE(luna_debug_ir_add_function(uncovered_location.Get(), 8U, 20U,
                                           View("bounded"),
                                           View("_L_626f756e646564"), false));
    EXPECT_FALSE(luna_debug_ir_verify(uncovered_location.Get(), 24U, nullptr));
}

} // namespace
} // namespace luna::test
