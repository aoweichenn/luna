#include "test_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luna::test {
namespace {

constexpr std::size_t LUNA_TEST_ELF_SECTION_HEADER_OFFSET = 40U;
constexpr std::size_t LUNA_TEST_ELF_SECTION_HEADER_SIZE_OFFSET = 58U;
constexpr std::size_t LUNA_TEST_ELF_SECTION_COUNT_OFFSET = 60U;
constexpr std::size_t LUNA_TEST_ELF_SECTION_NAMES_INDEX_OFFSET = 62U;
constexpr std::size_t LUNA_TEST_ELF_SECTION_HEADER_NAME_OFFSET = 0U;
constexpr std::size_t LUNA_TEST_ELF_SECTION_HEADER_TYPE_OFFSET = 4U;
constexpr std::size_t LUNA_TEST_ELF_SECTION_HEADER_FILE_OFFSET = 24U;
constexpr std::size_t LUNA_TEST_ELF_SECTION_HEADER_SIZE = 32U;
constexpr std::size_t LUNA_TEST_ELF_SECTION_HEADER_LINK_OFFSET = 40U;
constexpr std::size_t LUNA_TEST_ELF_SECTION_HEADER_INFO_OFFSET = 44U;
constexpr std::size_t LUNA_TEST_ELF_RELOCATION_ENTRY_SIZE = 24U;
constexpr std::uint32_t LUNA_TEST_ELF_SECTION_RELA = 4U;
constexpr std::uint32_t LUNA_TEST_RELOCATION_PC32 = 2U;
constexpr std::uint32_t LUNA_TEST_RELOCATION_PLT32 = 4U;

struct ElfSection final {
    std::string name;
    std::uint32_t type;
    std::uint64_t offset;
    std::uint64_t size;
    std::uint32_t link;
    std::uint32_t info;
};

[[nodiscard]] std::uint16_t ReadU16(std::string_view bytes,
                                    std::size_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(
                static_cast<unsigned char>(bytes[offset + 1U]))
            << 8U));
}

[[nodiscard]] std::uint32_t ReadU32(std::string_view bytes,
                                    std::size_t offset) {
    std::uint32_t value = 0U;
    for (std::uint32_t index = 0U; index < 4U; index += 1U) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(
                     bytes[offset + static_cast<std::size_t>(index)]))
                 << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint64_t ReadU64(std::string_view bytes,
                                    std::size_t offset) {
    std::uint64_t value = 0U;
    for (std::uint32_t index = 0U; index < 8U; index += 1U) {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(
                     bytes[offset + static_cast<std::size_t>(index)]))
                 << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::string ReadString(std::string_view bytes, std::size_t offset,
                                     std::size_t limit) {
    std::size_t end = offset;
    while (end < limit && bytes[end] != '\0') {
        end += 1U;
    }
    return std::string{bytes.substr(offset, end - offset)};
}

[[nodiscard]] std::vector<ElfSection> ReadSections(std::string_view object) {
    const std::uint64_t section_header_offset =
        ReadU64(object, LUNA_TEST_ELF_SECTION_HEADER_OFFSET);
    const std::uint16_t section_header_size =
        ReadU16(object, LUNA_TEST_ELF_SECTION_HEADER_SIZE_OFFSET);
    const std::uint16_t section_count =
        ReadU16(object, LUNA_TEST_ELF_SECTION_COUNT_OFFSET);
    const std::uint16_t names_index =
        ReadU16(object, LUNA_TEST_ELF_SECTION_NAMES_INDEX_OFFSET);
    const std::size_t names_header =
        static_cast<std::size_t>(section_header_offset) +
        static_cast<std::size_t>(names_index) * section_header_size;
    const std::uint64_t names_offset = ReadU64(
        object, names_header + LUNA_TEST_ELF_SECTION_HEADER_FILE_OFFSET);
    const std::uint64_t names_size =
        ReadU64(object, names_header + LUNA_TEST_ELF_SECTION_HEADER_SIZE);

    std::vector<ElfSection> sections;
    sections.reserve(section_count);
    for (std::uint16_t index = 0U; index < section_count; index += 1U) {
        const std::size_t header =
            static_cast<std::size_t>(section_header_offset) +
            static_cast<std::size_t>(index) * section_header_size;
        const std::uint32_t name_offset =
            ReadU32(object, header + LUNA_TEST_ELF_SECTION_HEADER_NAME_OFFSET);
        sections.push_back(ElfSection{
            .name = ReadString(
                object, static_cast<std::size_t>(names_offset) + name_offset,
                static_cast<std::size_t>(names_offset + names_size)),
            .type = ReadU32(object,
                            header + LUNA_TEST_ELF_SECTION_HEADER_TYPE_OFFSET),
            .offset = ReadU64(
                object, header + LUNA_TEST_ELF_SECTION_HEADER_FILE_OFFSET),
            .size = ReadU64(object, header + LUNA_TEST_ELF_SECTION_HEADER_SIZE),
            .link = ReadU32(object,
                            header + LUNA_TEST_ELF_SECTION_HEADER_LINK_OFFSET),
            .info = ReadU32(object,
                            header + LUNA_TEST_ELF_SECTION_HEADER_INFO_OFFSET),
        });
    }
    return sections;
}

[[nodiscard]] std::optional<ElfSection>
FindSection(const std::vector<ElfSection> &sections, std::string_view name) {
    for (const ElfSection &section : sections) {
        if (section.name == name) {
            return section;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool VerifyObject(std::string_view object) {
    return luna_x86_64_elf_object_verify(
        LunaStringView{
            .data = object.data(),
            .length = object.size(),
        },
        nullptr);
}

TEST(X8664ElfObjectTest, EmitsVerifiedSectionsSymbolsAndRelocations) {
    FrontendHarness harness{"module test.native_object;\n"
                            "extern fn c_i32_identity(value: i32) -> i32;\n"
                            "fn main() -> i32 {\n"
                            "    let text: *const u8 = \"A\";\n"
                            "    return c_i32_identity(text[0] as i32);\n"
                            "}\n"};
    ASSERT_TRUE(harness.EmitObject()) << harness.Diagnostics();
    const std::string object = harness.Object();
    const std::string expected_magic{"\x7f"
                                     "ELF",
                                     4U};
    ASSERT_GE(object.size(), 64U);
    EXPECT_EQ(object.substr(0U, 4U), expected_magic);
    EXPECT_TRUE(VerifyObject(object));

    const std::vector<ElfSection> sections = ReadSections(object);
    EXPECT_TRUE(FindSection(sections, ".text").has_value());
    EXPECT_TRUE(FindSection(sections, ".rodata").has_value());
    EXPECT_TRUE(FindSection(sections, ".symtab").has_value());
    EXPECT_TRUE(FindSection(sections, ".strtab").has_value());
    EXPECT_TRUE(FindSection(sections, ".note.GNU-stack").has_value());
    const std::optional<ElfSection> relocations =
        FindSection(sections, ".rela.text");
    ASSERT_TRUE(relocations.has_value());
    ASSERT_EQ(relocations->type, LUNA_TEST_ELF_SECTION_RELA);
    ASSERT_EQ(relocations->size % LUNA_TEST_ELF_RELOCATION_ENTRY_SIZE, 0U);
    ASSERT_EQ(relocations->size / LUNA_TEST_ELF_RELOCATION_ENTRY_SIZE, 2U);

    bool found_pc32 = false;
    bool found_plt32 = false;
    for (std::uint64_t offset = 0U; offset < relocations->size;
         offset += LUNA_TEST_ELF_RELOCATION_ENTRY_SIZE) {
        const std::uint64_t info = ReadU64(
            object,
            static_cast<std::size_t>(relocations->offset + offset) + 8U);
        const std::uint32_t type = static_cast<std::uint32_t>(info);
        found_pc32 = found_pc32 || type == LUNA_TEST_RELOCATION_PC32;
        found_plt32 = found_plt32 || type == LUNA_TEST_RELOCATION_PLT32;
    }
    EXPECT_TRUE(found_pc32);
    EXPECT_TRUE(found_plt32);

    std::string corrupted_addend = object;
    corrupted_addend[static_cast<std::size_t>(relocations->offset) + 16U] ^=
        static_cast<char>(UINT8_C(1));
    EXPECT_FALSE(VerifyObject(corrupted_addend));
}

TEST(X8664ElfObjectTest, ResolvesNamedAndNumericTextLabelsWithoutRelocations) {
    constexpr std::string_view LUNA_TEST_ASSEMBLY =
        "    .data\n"
        "    .globl mutable_value\n"
        "    .type mutable_value, @object\n"
        "    .balign 8\n"
        "mutable_value:\n"
        "    .byte 1\n"
        "    .size mutable_value, .-mutable_value\n"
        "    .text\n"
        "    .globl _start\n"
        "    .type _start, @function\n"
        "_start:\n"
        "    jmp 1f\n"
        "2:\n"
        "    ret\n"
        "1:\n"
        "    jne 2b\n"
        "    jmp .Ldone\n"
        ".Ldone:\n"
        "    ret\n"
        "    .size _start, .-_start\n"
        "    .section .note.GNU-stack,\"\",@progbits\n";
    std::FILE *diagnostics_file = std::tmpfile();
    ASSERT_NE(diagnostics_file, nullptr);
    LunaDiagnosticEngine diagnostics{};
    luna_diagnostic_init(&diagnostics, diagnostics_file);
    LunaStringBuilder object{};
    luna_string_builder_init(&object);
    ASSERT_TRUE(luna_x86_64_assemble_elf_object(
        LunaStringView{
            .data = LUNA_TEST_ASSEMBLY.data(),
            .length = LUNA_TEST_ASSEMBLY.size(),
        },
        &diagnostics, &object));
    const std::string bytes{luna_string_builder_data(&object), object.length};
    EXPECT_TRUE(VerifyObject(bytes));
    const std::vector<ElfSection> sections = ReadSections(bytes);
    EXPECT_TRUE(FindSection(sections, ".data").has_value());
    EXPECT_FALSE(FindSection(sections, ".rela.text").has_value());
    luna_string_builder_destroy(&object);
    EXPECT_EQ(std::fclose(diagnostics_file), 0);
}

TEST(X8664ElfObjectTest, EncodesRepresentativeIntegerSseAndStringInstructions) {
    constexpr std::string_view LUNA_TEST_ASSEMBLY =
        "    .text\n"
        "    .type encoded, @function\n"
        "encoded:\n"
        "    movabsq $0x1122334455667788, %r12\n"
        "    movq %r12, -8(%rbp)\n"
        "    movss %xmm9, 8(%r11)\n"
        "    cvttsd2siq %xmm0, %rax\n"
        "    shrq $8, %r10\n"
        "    movb $-1, %spl\n"
        "    cmpb $-1, %r12b\n"
        "    movw $0x1234, %r12w\n"
        "    rep movsb\n"
        "    syscall\n"
        "    ret\n"
        "    .size encoded, .-encoded\n"
        "    .section .note.GNU-stack,\"\",@progbits\n";
    constexpr std::array<std::uint8_t, 48U> LUNA_TEST_EXPECTED_TEXT = {
        0x49U, 0xbcU, 0x88U, 0x77U, 0x66U, 0x55U, 0x44U, 0x33U, 0x22U, 0x11U,
        0x4cU, 0x89U, 0x65U, 0xf8U, 0xf3U, 0x45U, 0x0fU, 0x11U, 0x4bU, 0x08U,
        0xf2U, 0x48U, 0x0fU, 0x2cU, 0xc0U, 0x49U, 0xc1U, 0xeaU, 0x08U, 0x40U,
        0xc6U, 0xc4U, 0xffU, 0x41U, 0x80U, 0xfcU, 0xffU, 0x66U, 0x41U, 0xc7U,
        0xc4U, 0x34U, 0x12U, 0xf3U, 0xa4U, 0x0fU, 0x05U, 0xc3U,
    };
    std::FILE *diagnostics_file = std::tmpfile();
    ASSERT_NE(diagnostics_file, nullptr);
    LunaDiagnosticEngine diagnostics{};
    luna_diagnostic_init(&diagnostics, diagnostics_file);
    LunaStringBuilder object{};
    luna_string_builder_init(&object);
    ASSERT_TRUE(luna_x86_64_assemble_elf_object(
        LunaStringView{
            .data = LUNA_TEST_ASSEMBLY.data(),
            .length = LUNA_TEST_ASSEMBLY.size(),
        },
        &diagnostics, &object));
    const std::string bytes{luna_string_builder_data(&object), object.length};
    const std::optional<ElfSection> text =
        FindSection(ReadSections(bytes), ".text");
    ASSERT_TRUE(text.has_value());
    ASSERT_EQ(text->size, LUNA_TEST_EXPECTED_TEXT.size());
    for (std::size_t index = 0U; index < LUNA_TEST_EXPECTED_TEXT.size();
         index += 1U) {
        EXPECT_EQ(static_cast<std::uint8_t>(
                      bytes[static_cast<std::size_t>(text->offset) + index]),
                  LUNA_TEST_EXPECTED_TEXT[index])
            << "byte " << index;
    }
    luna_string_builder_destroy(&object);
    EXPECT_EQ(std::fclose(diagnostics_file), 0);
}

TEST(X8664ElfObjectTest, IsDeterministicForIdenticalTypedIr) {
    constexpr std::string_view LUNA_TEST_SOURCE =
        "module test.deterministic_object;\n"
        "fn main() -> i32 { return 42; }\n";
    FrontendHarness first{LUNA_TEST_SOURCE};
    FrontendHarness second{LUNA_TEST_SOURCE};
    ASSERT_TRUE(first.EmitObject()) << first.Diagnostics();
    ASSERT_TRUE(second.EmitObject()) << second.Diagnostics();
    EXPECT_EQ(first.Object(), second.Object());
}

TEST(X8664ElfObjectTest, RejectsInstructionsOutsideTheOwnedDialect) {
    constexpr std::string_view LUNA_TEST_ASSEMBLY =
        "    .text\n"
        "invalid_instruction %rax\n";
    std::FILE *diagnostics_file = std::tmpfile();
    ASSERT_NE(diagnostics_file, nullptr);
    LunaDiagnosticEngine diagnostics{};
    luna_diagnostic_init(&diagnostics, diagnostics_file);
    LunaStringBuilder object{};
    luna_string_builder_init(&object);
    EXPECT_FALSE(luna_x86_64_assemble_elf_object(
        LunaStringView{
            .data = LUNA_TEST_ASSEMBLY.data(),
            .length = LUNA_TEST_ASSEMBLY.size(),
        },
        &diagnostics, &object));
    EXPECT_EQ(object.length, 0U);
    EXPECT_EQ(luna_diagnostic_error_count(&diagnostics), 1U);
    luna_string_builder_destroy(&object);
    EXPECT_EQ(std::fclose(diagnostics_file), 0);
}

TEST(X8664ElfObjectTest, RejectsPathologicalAlignmentAndInvalidSymbolNames) {
    constexpr std::array<std::string_view, 2U> LUNA_TEST_ASSEMBLIES = {
        "    .text\n"
        "    .p2align 13\n",
        "    .text\n"
        "invalid-symbol:\n"
        "    ret\n",
    };
    std::FILE *diagnostics_file = std::tmpfile();
    ASSERT_NE(diagnostics_file, nullptr);
    LunaDiagnosticEngine diagnostics{};
    luna_diagnostic_init(&diagnostics, diagnostics_file);
    for (const std::string_view assembly : LUNA_TEST_ASSEMBLIES) {
        LunaStringBuilder object{};
        luna_string_builder_init(&object);
        EXPECT_FALSE(luna_x86_64_assemble_elf_object(
            LunaStringView{
                .data = assembly.data(),
                .length = assembly.size(),
            },
            &diagnostics, &object));
        EXPECT_EQ(object.length, 0U);
        luna_string_builder_destroy(&object);
    }
    EXPECT_EQ(luna_diagnostic_error_count(&diagnostics),
              LUNA_TEST_ASSEMBLIES.size());
    EXPECT_EQ(std::fclose(diagnostics_file), 0);
}

TEST(X8664ElfObjectTest, VerifierRejectsCorruptedHeaderOffsets) {
    FrontendHarness harness{"module test.corrupt_object;\n"
                            "fn main() -> i32 { return 42; }\n"};
    ASSERT_TRUE(harness.EmitObject()) << harness.Diagnostics();
    std::string object = harness.Object();
    ASSERT_TRUE(VerifyObject(object));

    object[LUNA_TEST_ELF_SECTION_HEADER_OFFSET] =
        static_cast<char>(UINT8_C(0xff));
    EXPECT_FALSE(VerifyObject(object));
}

}
}
