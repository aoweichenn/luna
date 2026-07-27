#include "test_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>

namespace luna::test {
namespace {

constexpr std::uint64_t LUNA_TEST_MUTATION_SEED = UINT64_C(0x4c554e41);
constexpr std::uint64_t LUNA_TEST_MUTATION_CASES = 1000U;
constexpr std::size_t LUNA_TEST_MAX_INPUT_SIZE = 4096U;

[[nodiscard]] std::size_t RandomIndex(std::mt19937_64 &random_engine,
                                      std::size_t upper_bound) {
    std::uniform_int_distribution<std::size_t> distribution{0U,
                                                            upper_bound - 1U};
    return distribution(random_engine);
}

void Mutate(std::string &input, std::mt19937_64 &random_engine) {
    const std::uint64_t mutation_count = 1U + random_engine() % 8U;
    for (std::uint64_t mutation = 0U; mutation < mutation_count;
         mutation += 1U) {
        const std::uint64_t operation = random_engine() % 4U;
        const char random_byte =
            static_cast<char>(random_engine() & UINT64_C(0xff));

        if (operation == 0U && !input.empty()) {
            input[RandomIndex(random_engine, input.size())] = random_byte;
        } else if (operation == 1U && input.size() < LUNA_TEST_MAX_INPUT_SIZE) {
            const std::size_t index =
                input.empty() ? 0U
                              : RandomIndex(random_engine, input.size() + 1U);
            input.insert(input.begin() + static_cast<std::ptrdiff_t>(index),
                         random_byte);
        } else if (operation == 2U && !input.empty()) {
            input.erase(RandomIndex(random_engine, input.size()), 1U);
        } else if (!input.empty()) {
            const std::size_t index = RandomIndex(random_engine, input.size());
            input[index] =
                static_cast<char>(static_cast<unsigned char>(input[index]) ^
                                  static_cast<unsigned char>(random_byte));
        }
    }
}

}

TEST(MutationFuzzTest, CleanFrontendResultsAlwaysProduceValidTypedIr) {
    constexpr std::array<std::string_view, 15U> LUNA_TEST_SEEDS = {
        "module fuzz.empty;\nfn main() -> i32 { return 0; }\n",
        "module fuzz.call;\n"
        "fn id(value: i32) -> i32 { return value; }\n"
        "fn main() -> i32 { return id(id(42)); }\n",
        "module fuzz.loop;\n"
        "fn main() -> i32 {\n"
        " var n: i32 = 0; while (n < 10) { n += 1; } return n;\n"
        "}\n",
        "module fuzz.wide;\n"
        "fn wide(value: i32) -> i64 { return ((value as i64) * 3) >> 1; }\n"
        "fn main() -> i32 {\n"
        " if (wide(2147483647) == 3221225470) { return 42; }\n"
        " return 1;\n"
        "}\n",
        "module fuzz.unsigned;\n"
        "fn wide(value: u64) -> u64 { return (value / 3) >> 1; }\n"
        "fn main() -> i32 {\n"
        " if (wide(18446744073709551615) > 0) { return 42; }\n"
        " return 1;\n"
        "}\n",
        "module fuzz.narrow;\n"
        "fn mix(value: i8, count: i8) -> u16 {\n"
        " return ((value >> count) as u8) as u16;\n"
        "}\n"
        "fn main() -> i32 {\n"
        " if (mix(-64, 9) == 224) { return 42; }\n"
        " return 1;\n"
        "}\n",
        "module fuzz.pointer_sized;\n"
        "fn mix(offset: isize, size: usize) -> usize {\n"
        " return ((offset * 3) as usize) + size;\n"
        "}\n"
        "fn main() -> i32 {\n"
        " if (mix(-4, 20) == 8) { return 42; }\n"
        " return 1;\n"
        "}\n",
        "module fuzz.floating;\n"
        "fn mix(left: f32, right: f32, scale: f64) -> f64 {\n"
        " let value: f32 = (left + right) * 0.5;\n"
        " if (value >= 0.0) { return scale / scale; }\n"
        " return 0.0 / 0.0;\n"
        "}\n"
        "fn main() -> i32 {\n"
        " if (mix(1.25, 2.75, 4.0) == 1.0) { return 42; }\n"
        " return 1;\n"
        "}\n",
        "module fuzz.scalar_conversions;\n"
        "fn mix(value: i64, real: f64) -> i64 {\n"
        " let converted: f32 = value as f32;\n"
        " return (converted as i64) + (real as i64);\n"
        "}\n"
        "fn main() -> i32 {\n"
        " return mix(-42, 2.75) as i32;\n"
        "}\n",
        "module fuzz.structured_control_flow;\n"
        "fn main() -> i32 {\n"
        " var value: i32 = 0;\n"
        " do { value += 1; } while (value < 2);\n"
        " for (var index: i32 = 0; index < 4; index += 1) {\n"
        "  switch (index) {\n"
        "   case 0, 2 { continue; }\n"
        "   case 3 { break; }\n"
        "   default { value += true ? index : 0; }\n"
        "  }\n"
        " }\n"
        " return value;\n"
        "}\n",
        "export module fuzz.interface;\nimport other.module;\n",
        "module fuzz.memory;\n"
        "fn main() -> i32 {\n"
        " var values: [4]i32 = {}; values[2] = 42;\n"
        " let pointer: *i32 = &values[0];\n"
        " let text: *const u8 = \"fuzz\\n\";\n"
        " if (pointer[2] == 42 && text[4] == 10) { return 42; }\n"
        " return 1;\n"
        "}\n",
        "module fuzz.external;\n"
        "extern fn c_mix(value: i16, size: usize, scale: f64) -> bool;\n"
        "fn main() -> i32 {\n"
        " return c_mix(-7, 4096, 1.25) ? 42 : 1;\n"
        "}\n",
        "module fuzz.aggregate;\n"
        "enum Kind: u8 { empty, ready = 7, }\n"
        "struct Pair { kind: Kind; byte: u8; value: i32; }\n"
        "fn main() -> i32 {\n"
        " var pair: Pair = { value = 42, kind = Kind.ready, };\n"
        " var copy: Pair = pair; copy = pair;\n"
        " let pointer: *Pair = &copy;\n"
        " if (sizeof(Pair) != 8 || alignof(Pair) != 4 ||\n"
        "     offsetof(Pair, value) != 4) { return 1; }\n"
        " return pointer->kind == Kind.ready ? pointer->value : 2;\n"
        "}\n",
        "",
    };

    std::mt19937_64 random_engine{LUNA_TEST_MUTATION_SEED};
    for (std::uint64_t case_index = 0U; case_index < LUNA_TEST_MUTATION_CASES;
         case_index += 1U) {
        const std::size_t seed_index =
            RandomIndex(random_engine, LUNA_TEST_SEEDS.size());
        std::string input{LUNA_TEST_SEEDS[seed_index]};
        Mutate(input, random_engine);

        FrontendHarness harness{std::string_view(input.data(), input.size())};
        ASSERT_TRUE(harness.IsReady()) << "case " << case_index;
        if (!harness.ParseAndLower()) {
            continue;
        }

        EXPECT_TRUE(harness.Verify())
            << "case " << case_index << ", seed " << seed_index << '\n'
            << harness.Diagnostics();
        EXPECT_TRUE(harness.EmitAssembly())
            << "case " << case_index << ", seed " << seed_index << '\n'
            << harness.Diagnostics();
    }
}

}
