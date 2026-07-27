#include "test_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <string_view>

namespace luna::test {
namespace {

constexpr std::uint64_t LUNA_TEST_MUTATION_SEED = UINT64_C(0x4c554e41);
constexpr std::uint64_t LUNA_TEST_MUTATION_CASES = 1000U;
constexpr std::uint64_t LUNA_TEST_MODULE_MUTATION_SEED =
    UINT64_C(0x4d4f44554c45);
constexpr std::uint64_t LUNA_TEST_MODULE_MUTATION_CASES = 500U;
constexpr std::uint64_t LUNA_TEST_MODULE_GRAPH_MUTATION_SEED =
    UINT64_C(0x4d4f44554c454752);
constexpr std::uint64_t LUNA_TEST_MODULE_GRAPH_MUTATION_CASES = 300U;
constexpr std::uint64_t LUNA_TEST_METADATA_MUTATION_SEED =
    UINT64_C(0x4d45544144415441);
constexpr std::uint64_t LUNA_TEST_METADATA_MUTATION_CASES = 500U;
constexpr std::size_t LUNA_TEST_MAX_INPUT_SIZE = 4096U;
constexpr std::size_t LUNA_TEST_METADATA_HEADER_SIZE = 32U;
constexpr std::size_t LUNA_TEST_METADATA_ARENA_BLOCK_SIZE =
    std::size_t{32U} * 1024U;
constexpr std::uint64_t LUNA_TEST_METADATA_HASH_OFFSET =
    UINT64_C(14695981039346656037);
constexpr std::uint64_t LUNA_TEST_METADATA_HASH_PRIME = UINT64_C(1099511628211);

struct FileCloser final {
    void operator()(std::FILE *file) const noexcept {
        if (file != nullptr) {
            static_cast<void>(std::fclose(file));
        }
    }
};

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

[[nodiscard]] std::uint64_t MetadataHash(std::string_view metadata,
                                         std::string_view payload) {
    std::uint64_t hash = LUNA_TEST_METADATA_HASH_OFFSET;
    for (std::size_t index = 0U; index < 8U; index += 1U) {
        hash ^= static_cast<std::uint64_t>(
            static_cast<unsigned char>(metadata[index]));
        hash *= LUNA_TEST_METADATA_HASH_PRIME;
    }
    for (std::size_t index = 12U; index < 16U; index += 1U) {
        hash ^= static_cast<std::uint64_t>(
            static_cast<unsigned char>(metadata[index]));
        hash *= LUNA_TEST_METADATA_HASH_PRIME;
    }
    for (const char value : payload) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(value));
        hash *= LUNA_TEST_METADATA_HASH_PRIME;
    }
    return hash;
}

void StoreU64Little(std::string &bytes, std::size_t offset,
                    std::uint64_t value) {
    for (std::uint32_t index = 0U; index < 8U; index += 1U) {
        bytes[offset + static_cast<std::size_t>(index)] =
            static_cast<char>((value >> (index * 8U)) & UINT64_C(0xff));
    }
}

void MutateMetadataPayload(std::string &metadata,
                           std::mt19937_64 &random_engine) {
    std::string payload = metadata.substr(LUNA_TEST_METADATA_HEADER_SIZE);
    Mutate(payload, random_engine);
    metadata.resize(LUNA_TEST_METADATA_HEADER_SIZE);
    metadata.append(payload);
    StoreU64Little(metadata, 16U, static_cast<std::uint64_t>(payload.size()));
    StoreU64Little(metadata, 24U, MetadataHash(metadata, payload));
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
        EXPECT_TRUE(harness.EmitMachineIr())
            << "case " << case_index << ", seed " << seed_index << '\n'
            << harness.Diagnostics();
        EXPECT_TRUE(harness.EmitAssembly())
            << "case " << case_index << ", seed " << seed_index << '\n'
            << harness.Diagnostics();
    }
}

TEST(MutationFuzzTest, CleanModulePairResultsAlwaysProduceValidTypedIr) {
    constexpr std::string_view LUNA_TEST_INTERFACE =
        "export module fuzz.module_pair;\n"
        "export struct Input { value: i32; }\n"
        "export fn calculate(input: *const Input, delta: i32) -> i32;\n"
        "export fn main() -> i32;\n";
    constexpr std::string_view LUNA_TEST_IMPLEMENTATION =
        "module fuzz.module_pair;\n"
        "fn calculate(data: *const Input, amount: i32) -> i32 {\n"
        " return data->value + amount;\n"
        "}\n"
        "fn main() -> i32 {\n"
        " var input: Input = { value = 20, };\n"
        " return calculate((&input) as *const Input, 22);\n"
        "}\n";

    FrontendHarness clean{LUNA_TEST_INTERFACE, LUNA_TEST_IMPLEMENTATION};
    ASSERT_TRUE(clean.Verify()) << clean.Diagnostics();
    ASSERT_TRUE(clean.EmitMachineIr()) << clean.Diagnostics();
    ASSERT_TRUE(clean.EmitAssembly()) << clean.Diagnostics();

    std::mt19937_64 random_engine{LUNA_TEST_MODULE_MUTATION_SEED};
    for (std::uint64_t case_index = 0U;
         case_index < LUNA_TEST_MODULE_MUTATION_CASES; case_index += 1U) {
        std::string interface_source{LUNA_TEST_INTERFACE};
        std::string implementation_source{LUNA_TEST_IMPLEMENTATION};
        if ((random_engine() & UINT64_C(1)) == 0U) {
            Mutate(interface_source, random_engine);
        } else {
            Mutate(implementation_source, random_engine);
        }

        FrontendHarness harness{
            std::string_view(interface_source.data(), interface_source.size()),
            std::string_view(implementation_source.data(),
                             implementation_source.size())};
        ASSERT_TRUE(harness.IsReady()) << "case " << case_index;
        if (!harness.ParseAndLower()) {
            continue;
        }
        EXPECT_TRUE(harness.Verify()) << "case " << case_index << '\n'
                                      << harness.Diagnostics();
        EXPECT_TRUE(harness.EmitMachineIr()) << "case " << case_index << '\n'
                                             << harness.Diagnostics();
        EXPECT_TRUE(harness.EmitAssembly()) << "case " << case_index << '\n'
                                            << harness.Diagnostics();
    }
}

TEST(MutationFuzzTest, CleanModuleGraphResultsAlwaysProduceValidTypedIr) {
    constexpr std::array<std::string_view, 5U> LUNA_TEST_MODULE_SOURCES = {
        "module fuzz.graph.app;\n"
        "import fuzz.graph.math;\n"
        "import fuzz.graph.core;\n"
        "fn main() -> i32 {\n"
        " var input: Input = { value = 20, };\n"
        " return calculate((&input) as *const Input);\n"
        "}\n",
        "export module fuzz.graph.core;\n"
        "export struct Input { value: i32; }\n"
        "export fn adjust(input: *const Input, delta: i32) -> i32;\n",
        "module fuzz.graph.math;\n"
        "fn calculate(input: *const Input) -> i32 {\n"
        " return adjust(input, 22);\n"
        "}\n",
        "export module fuzz.graph.math;\n"
        "import fuzz.graph.core;\n"
        "export fn calculate(input: *const Input) -> i32;\n",
        "module fuzz.graph.core;\n"
        "fn adjust(input: *const Input, delta: i32) -> i32 {\n"
        " return input->value + delta;\n"
        "}\n",
    };

    CompilationHarness clean{
        LUNA_TEST_MODULE_SOURCES[0], LUNA_TEST_MODULE_SOURCES[1],
        LUNA_TEST_MODULE_SOURCES[2], LUNA_TEST_MODULE_SOURCES[3],
        LUNA_TEST_MODULE_SOURCES[4]};
    ASSERT_TRUE(clean.Verify()) << clean.Diagnostics();
    ASSERT_TRUE(clean.EmitMachineIr()) << clean.Diagnostics();
    ASSERT_TRUE(clean.EmitAssembly()) << clean.Diagnostics();

    std::mt19937_64 random_engine{LUNA_TEST_MODULE_GRAPH_MUTATION_SEED};
    for (std::uint64_t case_index = 0U;
         case_index < LUNA_TEST_MODULE_GRAPH_MUTATION_CASES; case_index += 1U) {
        std::array<std::string, LUNA_TEST_MODULE_SOURCES.size()> sources;
        for (std::size_t index = 0U; index < sources.size(); index += 1U) {
            sources[index] = LUNA_TEST_MODULE_SOURCES[index];
        }
        const std::size_t mutated_source =
            RandomIndex(random_engine, sources.size());
        Mutate(sources[mutated_source], random_engine);

        CompilationHarness harness{
            std::string_view{sources[0]}, std::string_view{sources[1]},
            std::string_view{sources[2]}, std::string_view{sources[3]},
            std::string_view{sources[4]}};
        ASSERT_TRUE(harness.IsReady()) << "case " << case_index;
        if (!harness.ParseAndLower()) {
            continue;
        }
        EXPECT_TRUE(harness.Verify())
            << "case " << case_index << ", source " << mutated_source << '\n'
            << harness.Diagnostics();
        EXPECT_TRUE(harness.EmitMachineIr())
            << "case " << case_index << ", source " << mutated_source << '\n'
            << harness.Diagnostics();
        EXPECT_TRUE(harness.EmitAssembly())
            << "case " << case_index << ", source " << mutated_source << '\n'
            << harness.Diagnostics();
    }
}

TEST(MutationFuzzTest, MetadataDecoderHandlesRechecksummedPayloadMutations) {
    constexpr std::string_view LUNA_TEST_INTERFACE =
        "export module fuzz.metadata;\n"
        "import fuzz.dependency;\n"
        "export struct Record { value: i64; next: *const Record; }\n"
        "export enum Kind: i8 { missing = -1, ready = 7, }\n"
        "export fn inspect(value: *const Record, kind: Kind) -> i64;\n";
    constexpr std::string_view LUNA_TEST_IMPLEMENTATION =
        "module fuzz.metadata;\n"
        "fn inspect(value: *const Record, kind: Kind) -> i64 {\n"
        " return value->value + ((kind as i8) as i64);\n"
        "}\n";

    FrontendHarness clean{LUNA_TEST_INTERFACE, LUNA_TEST_IMPLEMENTATION};
    ASSERT_TRUE(clean.Parse()) << clean.Diagnostics();
    LunaStringBuilder encoded{};
    luna_string_builder_init(&encoded);
    const LunaModuleMetadataDependency dependencies[] = {
        {
            .module_name = luna_string_view_from_c_string("fuzz.dependency"),
            .content_hash = UINT64_C(0x123456789abcdef0),
        },
    };
    ASSERT_TRUE(luna_module_metadata_encode(
        clean.InterfaceProgram(), luna_target_info_default(), dependencies, 1U,
        clean.DiagnosticEngine(), &encoded))
        << clean.Diagnostics();
    const std::string valid_metadata{luna_string_builder_data(&encoded),
                                     encoded.length};
    luna_string_builder_destroy(&encoded);

    std::mt19937_64 random_engine{LUNA_TEST_METADATA_MUTATION_SEED};
    for (std::uint64_t case_index = 0U;
         case_index < LUNA_TEST_METADATA_MUTATION_CASES; case_index += 1U) {
        std::string mutated = valid_metadata;
        MutateMetadataPayload(mutated, random_engine);

        std::unique_ptr<std::FILE, FileCloser> diagnostic_file{std::tmpfile()};
        ASSERT_NE(diagnostic_file, nullptr) << "case " << case_index;
        LunaDiagnosticEngine diagnostics{};
        luna_diagnostic_init(&diagnostics, diagnostic_file.get());
        LunaArena arena{};
        luna_arena_init(&arena, LUNA_TEST_METADATA_ARENA_BLOCK_SIZE);
        LunaSourceFile source{};
        const bool source_ready = luna_source_from_bytes(
            "<mutation.lmi>", mutated.data(), mutated.size(), &source);
        EXPECT_TRUE(source_ready) << "case " << case_index;
        if (!source_ready) {
            luna_arena_destroy(&arena);
            continue;
        }
        LunaModuleMetadata metadata{};
        if (luna_module_metadata_decode(&source, luna_target_info_default(),
                                        &arena, &diagnostics, &metadata)) {
            LunaStringBuilder normalized{};
            luna_string_builder_init(&normalized);
            EXPECT_TRUE(luna_module_metadata_encode(
                metadata.interface_unit, luna_target_info_default(),
                reinterpret_cast<const LunaModuleMetadataDependency *>(
                    metadata.dependencies.data),
                static_cast<std::uint32_t>(metadata.dependencies.length),
                &diagnostics, &normalized))
                << "case " << case_index;

            LunaSourceFile normalized_source{};
            LunaModuleMetadata normalized_metadata{};
            if (normalized.length > 0U) {
                const bool normalized_source_ready = luna_source_from_bytes(
                    "<normalized.lmi>", luna_string_builder_data(&normalized),
                    normalized.length, &normalized_source);
                EXPECT_TRUE(normalized_source_ready) << "case " << case_index;
                if (normalized_source_ready) {
                    EXPECT_TRUE(luna_module_metadata_decode(
                        &normalized_source, luna_target_info_default(), &arena,
                        &diagnostics, &normalized_metadata))
                        << "case " << case_index;
                }
            }
            luna_module_metadata_destroy(&normalized_metadata);
            luna_source_destroy(&normalized_source);
            luna_string_builder_destroy(&normalized);
        }

        luna_module_metadata_destroy(&metadata);
        luna_source_destroy(&source);
        luna_arena_destroy(&arena);
    }
}

}
