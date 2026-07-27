#include "test_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace luna::test {
namespace {

constexpr std::size_t LUNA_TEST_ELF_TYPE_OFFSET = 16U;
constexpr std::size_t LUNA_TEST_ELF_ENTRY_OFFSET = 24U;
constexpr std::size_t LUNA_TEST_ELF_PROGRAM_HEADER_OFFSET = 32U;
constexpr std::size_t LUNA_TEST_ELF_PROGRAM_FLAGS_OFFSET = 4U;
constexpr std::size_t LUNA_TEST_ELF_PROGRAM_ADDRESS_OFFSET = 16U;
constexpr std::size_t LUNA_TEST_ELF_PROGRAM_FILE_SIZE_OFFSET = 32U;
constexpr std::size_t LUNA_TEST_ELF_PROGRAM_MEMORY_SIZE_OFFSET = 40U;
constexpr std::uint16_t LUNA_TEST_ELF_EXECUTABLE_TYPE = 2U;
constexpr std::uint32_t LUNA_TEST_ELF_READ_EXECUTE_FLAGS = 5U;
constexpr std::uint32_t LUNA_TEST_ELF_SECTION_SYMTAB = 2U;
constexpr std::uint32_t LUNA_TEST_ELF_SECTION_RELA = 4U;
constexpr std::size_t LUNA_TEST_ELF_SECTION_HEADER_SIZE = 64U;
constexpr std::size_t LUNA_TEST_ELF_SYMBOL_SIZE = 24U;
constexpr std::uint8_t LUNA_TEST_ELF_SYMBOL_WEAK = 2U;

struct FileCloser {
    void operator()(std::FILE *file) const noexcept {
        if (file != nullptr) {
            static_cast<void>(std::fclose(file));
        }
    }
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

void StoreU32(std::string &bytes, std::size_t offset, std::uint32_t value) {
    for (std::uint32_t index = 0U; index < 4U; index += 1U) {
        bytes[offset + static_cast<std::size_t>(index)] =
            static_cast<char>((value >> (index * 8U)) & UINT32_C(0xff));
    }
}

void StoreU64(std::string &bytes, std::size_t offset, std::uint64_t value) {
    for (std::uint32_t index = 0U; index < 8U; index += 1U) {
        bytes[offset + static_cast<std::size_t>(index)] =
            static_cast<char>((value >> (index * 8U)) & UINT64_C(0xff));
    }
}

[[nodiscard]] std::string_view
ReadString(std::string_view bytes, std::size_t offset, std::size_t limit) {
    std::size_t end = offset;
    while (end < limit && bytes[end] != '\0') {
        end += 1U;
    }
    return bytes.substr(offset, end - offset);
}

[[nodiscard]] std::optional<std::size_t>
FindSectionHeader(std::string_view object, std::uint32_t section_type) {
    const std::uint64_t section_headers = ReadU64(object, 40U);
    const std::uint16_t section_count = ReadU16(object, 60U);
    for (std::uint16_t index = 1U; index < section_count; index += 1U) {
        const std::size_t header =
            static_cast<std::size_t>(section_headers) +
            static_cast<std::size_t>(index) * LUNA_TEST_ELF_SECTION_HEADER_SIZE;
        if (ReadU32(object, header + 4U) == section_type) {
            return header;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t>
FindSectionHeaderByName(std::string_view object, std::string_view name) {
    const std::uint64_t section_headers = ReadU64(object, 40U);
    const std::uint16_t section_count = ReadU16(object, 60U);
    const std::uint16_t names_index = ReadU16(object, 62U);
    const std::size_t names_header = static_cast<std::size_t>(section_headers) +
                                     static_cast<std::size_t>(names_index) *
                                         LUNA_TEST_ELF_SECTION_HEADER_SIZE;
    const std::uint64_t names_offset = ReadU64(object, names_header + 24U);
    const std::uint64_t names_size = ReadU64(object, names_header + 32U);
    for (std::uint16_t index = 1U; index < section_count; index += 1U) {
        const std::size_t header =
            static_cast<std::size_t>(section_headers) +
            static_cast<std::size_t>(index) * LUNA_TEST_ELF_SECTION_HEADER_SIZE;
        const std::uint32_t name_offset = ReadU32(object, header);
        if (ReadString(
                object, static_cast<std::size_t>(names_offset) + name_offset,
                static_cast<std::size_t>(names_offset + names_size)) == name) {
            return header;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> FindSymbol(std::string_view object,
                                                    std::string_view name) {
    const std::optional<std::size_t> symbol_header =
        FindSectionHeader(object, LUNA_TEST_ELF_SECTION_SYMTAB);
    if (!symbol_header.has_value()) {
        return std::nullopt;
    }
    const std::uint64_t symbol_offset =
        ReadU64(object, symbol_header.value() + 24U);
    const std::uint64_t symbol_size =
        ReadU64(object, symbol_header.value() + 32U);
    const std::uint32_t string_index =
        ReadU32(object, symbol_header.value() + 40U);
    const std::uint64_t section_headers = ReadU64(object, 40U);
    const std::size_t string_header =
        static_cast<std::size_t>(section_headers) +
        static_cast<std::size_t>(string_index) *
            LUNA_TEST_ELF_SECTION_HEADER_SIZE;
    const std::uint64_t string_offset = ReadU64(object, string_header + 24U);
    const std::uint64_t string_size = ReadU64(object, string_header + 32U);

    for (std::uint64_t offset = 0U; offset < symbol_size;
         offset += LUNA_TEST_ELF_SYMBOL_SIZE) {
        const std::size_t entry =
            static_cast<std::size_t>(symbol_offset + offset);
        const std::uint32_t name_offset = ReadU32(object, entry);
        if (ReadString(object,
                       static_cast<std::size_t>(string_offset) + name_offset,
                       static_cast<std::size_t>(string_offset + string_size)) ==
            name) {
            return entry;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t>
FindFirstRelocation(std::string_view object) {
    const std::optional<std::size_t> relocation_header =
        FindSectionHeader(object, LUNA_TEST_ELF_SECTION_RELA);
    if (!relocation_header.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(
        ReadU64(object, relocation_header.value() + 24U));
}

[[nodiscard]] std::string ReadStream(std::FILE *stream) {
    if (stream == nullptr || std::fflush(stream) != 0) {
        return {};
    }
    if (std::fseek(stream, 0L, SEEK_SET) != 0) {
        return {};
    }
    std::string result;
    std::array<char, 1024U> buffer{};
    for (;;) {
        const std::size_t count =
            std::fread(buffer.data(), 1U, buffer.size(), stream);
        result.append(buffer.data(), count);
        if (count != buffer.size()) {
            break;
        }
    }
    return result;
}

[[nodiscard]] bool VerifyExecutable(std::string_view executable) {
    return luna_x86_64_elf_executable_verify(
        LunaStringView{
            .data = executable.data(),
            .length = executable.size(),
        },
        nullptr);
}

[[nodiscard]] std::string Assemble(std::string_view assembly) {
    std::FILE *diagnostic_file = std::tmpfile();
    if (diagnostic_file == nullptr) {
        return {};
    }
    LunaDiagnosticEngine diagnostics{};
    luna_diagnostic_init(&diagnostics, diagnostic_file);
    LunaStringBuilder object{};
    luna_string_builder_init(&object);
    const bool success = luna_x86_64_assemble_elf_object(
        LunaStringView{
            .data = assembly.data(),
            .length = assembly.size(),
        },
        &diagnostics, &object);
    std::string result;
    if (success) {
        result.assign(luna_string_builder_data(&object), object.length);
    }
    luna_string_builder_destroy(&object);
    static_cast<void>(std::fclose(diagnostic_file));
    return result;
}

[[nodiscard]] bool Link(const std::array<std::string_view, 3U> &objects,
                        std::uint32_t object_count,
                        LunaStringBuilder *executable,
                        std::FILE *diagnostic_file) {
    std::array<LunaX8664ElfLinkInput, 3U> inputs{};
    constexpr std::array<std::string_view, 3U> LUNA_TEST_NAMES = {
        "root.o",
        "library-a.o",
        "library-b.o",
    };
    for (std::uint32_t index = 0U; index < object_count; index += 1U) {
        inputs[index] = LunaX8664ElfLinkInput{
            .name =
                {
                    .data = LUNA_TEST_NAMES[index].data(),
                    .length = LUNA_TEST_NAMES[index].size(),
                },
            .object =
                {
                    .data = objects[index].data(),
                    .length = objects[index].size(),
                },
        };
    }
    return luna_x86_64_link_elf_executable(
        inputs.data(), object_count, luna_string_view_from_c_string("_start"),
        diagnostic_file, executable);
}

constexpr std::string_view LUNA_TEST_ROOT_ASSEMBLY =
    "    .extern answer\n"
    "    .text\n"
    "    .globl _start\n"
    "    .type _start, @function\n"
    "_start:\n"
    "    call answer\n"
    "    movl %eax, %edi\n"
    "    movq $60, %rax\n"
    "    syscall\n"
    "    .size _start, .-_start\n"
    "    .section .note.GNU-stack,\"\",@progbits\n";

constexpr std::string_view LUNA_TEST_LIBRARY_ASSEMBLY =
    "    .text\n"
    "    .globl answer\n"
    "    .type answer, @function\n"
    "answer:\n"
    "    movl $42, %eax\n"
    "    ret\n"
    "    .size answer, .-answer\n"
    "    .section .note.GNU-stack,\"\",@progbits\n";

constexpr std::string_view LUNA_TEST_SYSCALL_OVERRIDE_ASSEMBLY =
    "    .text\n"
    "    .globl _start\n"
    "    .type _start, @function\n"
    "_start:\n"
    "    movl $42, %edi\n"
    "    movq $60, %rax\n"
    "    syscall\n"
    "    .size _start, .-_start\n"
    "    .globl luna_linux_syscall0\n"
    "    .type luna_linux_syscall0, @function\n"
    "luna_linux_syscall0:\n"
    "    ret\n"
    "    .size luna_linux_syscall0, .-luna_linux_syscall0\n"
    "    .section .note.GNU-stack,\"\",@progbits\n";

TEST(X8664ElfLinkerTest, EmitsDeterministicVerifiedStaticExecutable) {
    FrontendHarness harness{"module test.native_link;\n"
                            "fn main() -> i32 { return 42; }\n"};
    ASSERT_TRUE(harness.EmitObject()) << harness.Diagnostics();
    const std::string object = harness.Object();
    const std::array<std::string_view, 3U> objects = {object, {}, {}};

    LunaStringBuilder first{};
    LunaStringBuilder second{};
    luna_string_builder_init(&first);
    luna_string_builder_init(&second);
    ASSERT_TRUE(Link(objects, 1U, &first, nullptr));
    ASSERT_TRUE(Link(objects, 1U, &second, nullptr));
    const std::string first_bytes{luna_string_builder_data(&first),
                                  first.length};
    const std::string second_bytes{luna_string_builder_data(&second),
                                   second.length};

    ASSERT_GE(first_bytes.size(), 64U);
    EXPECT_EQ(ReadU16(first_bytes, LUNA_TEST_ELF_TYPE_OFFSET),
              LUNA_TEST_ELF_EXECUTABLE_TYPE);
    EXPECT_NE(ReadU64(first_bytes, LUNA_TEST_ELF_ENTRY_OFFSET), 0U);
    const std::uint64_t program_headers =
        ReadU64(first_bytes, LUNA_TEST_ELF_PROGRAM_HEADER_OFFSET);
    EXPECT_EQ(ReadU32(first_bytes, static_cast<std::size_t>(program_headers) +
                                       LUNA_TEST_ELF_PROGRAM_FLAGS_OFFSET),
              LUNA_TEST_ELF_READ_EXECUTE_FLAGS);
    EXPECT_TRUE(VerifyExecutable(first_bytes));
    EXPECT_EQ(first_bytes, second_bytes);

    const std::optional<std::size_t> debug_info =
        FindSectionHeaderByName(first_bytes, ".debug_info");
    ASSERT_TRUE(debug_info.has_value());
    const std::uint64_t debug_info_offset =
        ReadU64(first_bytes, debug_info.value() + 24U);
    std::string corrupted_dwarf = first_bytes;
    corrupted_dwarf[static_cast<std::size_t>(debug_info_offset) + 4U] =
        static_cast<char>(UINT8_C(4));
    EXPECT_FALSE(VerifyExecutable(corrupted_dwarf));

    luna_string_builder_destroy(&second);
    luna_string_builder_destroy(&first);
}

TEST(X8664ElfLinkerTest, ResolvesCrossObjectCallRelocation) {
    const std::string root = Assemble(LUNA_TEST_ROOT_ASSEMBLY);
    const std::string library = Assemble(LUNA_TEST_LIBRARY_ASSEMBLY);
    ASSERT_FALSE(root.empty());
    ASSERT_FALSE(library.empty());
    const std::array<std::string_view, 3U> objects = {root, library, {}};

    LunaStringBuilder executable{};
    luna_string_builder_init(&executable);
    EXPECT_TRUE(Link(objects, 2U, &executable, nullptr));
    EXPECT_TRUE(VerifyExecutable(std::string_view{
        luna_string_builder_data(&executable), executable.length}));
    luna_string_builder_destroy(&executable);
}

TEST(X8664ElfLinkerTest, RejectsUndefinedAndDuplicateStrongSymbols) {
    const std::string root = Assemble(LUNA_TEST_ROOT_ASSEMBLY);
    const std::string library = Assemble(LUNA_TEST_LIBRARY_ASSEMBLY);
    ASSERT_FALSE(root.empty());
    ASSERT_FALSE(library.empty());
    const auto diagnostic_file =
        std::unique_ptr<std::FILE, FileCloser>{std::tmpfile()};
    ASSERT_NE(diagnostic_file, nullptr);

    LunaStringBuilder executable{};
    luna_string_builder_init(&executable);
    const std::array<std::string_view, 3U> undefined_objects = {root, {}, {}};
    EXPECT_FALSE(
        Link(undefined_objects, 1U, &executable, diagnostic_file.get()));
    EXPECT_EQ(executable.length, 0U);
    EXPECT_NE(
        ReadStream(diagnostic_file.get()).find("undefined symbol 'answer'"),
        std::string::npos);

    ASSERT_EQ(std::fseek(diagnostic_file.get(), 0L, SEEK_SET), 0);
    const std::array<std::string_view, 3U> duplicate_objects = {
        root,
        library,
        library,
    };
    EXPECT_FALSE(
        Link(duplicate_objects, 3U, &executable, diagnostic_file.get()));
    EXPECT_EQ(executable.length, 0U);
    EXPECT_NE(
        ReadStream(diagnostic_file.get()).find("duplicate strong definition"),
        std::string::npos);
    luna_string_builder_destroy(&executable);
}

TEST(X8664ElfLinkerTest, RejectsOverrideOfProjectOwnedSyscallAbi) {
    const std::string override_object =
        Assemble(LUNA_TEST_SYSCALL_OVERRIDE_ASSEMBLY);
    ASSERT_FALSE(override_object.empty());
    const std::array<std::string_view, 3U> objects = {
        override_object,
        {},
        {},
    };
    const auto diagnostic_file =
        std::unique_ptr<std::FILE, FileCloser>{std::tmpfile()};
    ASSERT_NE(diagnostic_file, nullptr);

    LunaStringBuilder executable{};
    luna_string_builder_init(&executable);
    EXPECT_FALSE(Link(objects, 1U, &executable, diagnostic_file.get()));
    EXPECT_EQ(executable.length, 0U);
    const std::string diagnostics = ReadStream(diagnostic_file.get());
    EXPECT_NE(diagnostics.find("duplicate strong definition of "
                               "'luna_linux_syscall0'"),
              std::string::npos);
    EXPECT_NE(diagnostics.find("<luna-linux-syscall-abi>"), std::string::npos);
    luna_string_builder_destroy(&executable);
}

TEST(X8664ElfLinkerTest, RejectsMalformedInputAndCorruptedExecutable) {
    constexpr std::string_view LUNA_TEST_MALFORMED = "\x7f"
                                                     "ELF";
    const std::array<std::string_view, 3U> malformed_objects = {
        LUNA_TEST_MALFORMED,
        {},
        {},
    };
    LunaStringBuilder executable{};
    luna_string_builder_init(&executable);
    EXPECT_FALSE(Link(malformed_objects, 1U, &executable, nullptr));

    FrontendHarness harness{"module test.corrupt_link;\n"
                            "fn main() -> i32 { return 42; }\n"};
    ASSERT_TRUE(harness.EmitObject()) << harness.Diagnostics();
    const std::string object = harness.Object();
    const std::array<std::string_view, 3U> objects = {object, {}, {}};
    ASSERT_TRUE(Link(objects, 1U, &executable, nullptr));
    std::string corrupted{luna_string_builder_data(&executable),
                          executable.length};
    corrupted[LUNA_TEST_ELF_PROGRAM_HEADER_OFFSET] =
        static_cast<char>(UINT8_C(0xff));
    EXPECT_FALSE(VerifyExecutable(corrupted));

    std::string entry_outside_text{luna_string_builder_data(&executable),
                                   executable.length};
    const std::uint64_t program_headers =
        ReadU64(entry_outside_text, LUNA_TEST_ELF_PROGRAM_HEADER_OFFSET);
    const std::uint64_t text_address =
        ReadU64(entry_outside_text, static_cast<std::size_t>(program_headers) +
                                        LUNA_TEST_ELF_PROGRAM_ADDRESS_OFFSET);
    const std::uint64_t text_size =
        ReadU64(entry_outside_text, static_cast<std::size_t>(program_headers) +
                                        LUNA_TEST_ELF_PROGRAM_FILE_SIZE_OFFSET);
    StoreU64(entry_outside_text, LUNA_TEST_ELF_ENTRY_OFFSET,
             text_address + text_size);
    StoreU64(entry_outside_text,
             static_cast<std::size_t>(program_headers) +
                 LUNA_TEST_ELF_PROGRAM_MEMORY_SIZE_OFFSET,
             text_size + 1U);
    EXPECT_FALSE(VerifyExecutable(entry_outside_text));

    std::string foreign_debug = object;
    const std::size_t debug_name = foreign_debug.find(".luna.debug");
    ASSERT_NE(debug_name, std::string::npos);
    foreign_debug.replace(debug_name, std::string_view{".luna.debug"}.size(),
                          ".debug_info");
    const std::array<std::string_view, 3U> foreign_objects = {
        foreign_debug,
        {},
        {},
    };
    LunaStringBuilder rejected{};
    luna_string_builder_init(&rejected);
    EXPECT_FALSE(Link(foreign_objects, 1U, &rejected, nullptr));
    EXPECT_EQ(rejected.length, 0U);
    luna_string_builder_destroy(&rejected);
    luna_string_builder_destroy(&executable);
}

TEST(X8664ElfLinkerTest, RejectsInvalidArgumentsAndClearsOutput) {
    constexpr std::string_view LUNA_TEST_NAME = "oversized.o";
    constexpr std::string_view LUNA_TEST_BYTE = "x";
    constexpr std::size_t LUNA_TEST_OVERSIZED_OBJECT =
        static_cast<std::size_t>(LUNA_X86_64_ELF_LINK_MAX_OBJECT_SIZE) + 1U;
    const LunaX8664ElfLinkInput input = {
        .name =
            {
                .data = LUNA_TEST_NAME.data(),
                .length = LUNA_TEST_NAME.size(),
            },
        .object =
            {
                .data = LUNA_TEST_BYTE.data(),
                .length = LUNA_TEST_OVERSIZED_OBJECT,
            },
    };
    LunaStringBuilder output{};
    luna_string_builder_init(&output);
    EXPECT_FALSE(luna_x86_64_link_elf_executable(
        &input, 1U, luna_string_view_from_c_string("_start"), nullptr,
        &output));
    ASSERT_TRUE(luna_string_builder_append_c_string(&output, "unchanged"));
    EXPECT_FALSE(luna_x86_64_link_elf_executable(
        nullptr, 1U, luna_string_view_from_c_string("_start"), nullptr,
        &output));
    EXPECT_EQ(output.length, 0U);
    luna_string_builder_destroy(&output);
}

TEST(X8664ElfLinkerTest, AppliesEverySupportedAbsoluteRelocationKind) {
    const std::string original_root = Assemble(LUNA_TEST_ROOT_ASSEMBLY);
    const std::string library = Assemble(LUNA_TEST_LIBRARY_ASSEMBLY);
    ASSERT_FALSE(original_root.empty());
    ASSERT_FALSE(library.empty());
    const std::optional<std::size_t> relocation =
        FindFirstRelocation(original_root);
    ASSERT_TRUE(relocation.has_value());
    const std::size_t relocation_offset = relocation.value_or(0U);

    constexpr std::array<std::uint32_t, 3U> LUNA_TEST_RELOCATIONS = {
        1U,
        10U,
        11U,
    };
    for (const std::uint32_t relocation_type : LUNA_TEST_RELOCATIONS) {
        std::string root = original_root;
        StoreU32(root, relocation_offset + 8U, relocation_type);
        StoreU64(root, relocation_offset + 16U, 0U);
        const std::array<std::string_view, 3U> objects = {
            root,
            library,
            {},
        };
        LunaStringBuilder executable{};
        luna_string_builder_init(&executable);
        EXPECT_TRUE(Link(objects, 2U, &executable, nullptr))
            << "relocation " << relocation_type;
        EXPECT_TRUE(VerifyExecutable(std::string_view{
            luna_string_builder_data(&executable), executable.length}))
            << "relocation " << relocation_type;
        luna_string_builder_destroy(&executable);
    }
}

TEST(X8664ElfLinkerTest, ImplementsWeakResolutionAndRejectsRelocationOverflow) {
    const std::string original_root = Assemble(LUNA_TEST_ROOT_ASSEMBLY);
    const std::string original_library = Assemble(LUNA_TEST_LIBRARY_ASSEMBLY);
    ASSERT_FALSE(original_root.empty());
    ASSERT_FALSE(original_library.empty());

    std::string weak_root = original_root;
    const std::optional<std::size_t> root_answer =
        FindSymbol(weak_root, "answer");
    ASSERT_TRUE(root_answer.has_value());
    const std::size_t root_answer_offset = root_answer.value_or(0U);
    const std::uint8_t root_type =
        static_cast<std::uint8_t>(weak_root[root_answer_offset + 4U]) &
        UINT8_C(0x0f);
    weak_root[root_answer_offset + 4U] = static_cast<char>(
        static_cast<std::uint8_t>(LUNA_TEST_ELF_SYMBOL_WEAK << 4U) | root_type);
    const std::array<std::string_view, 3U> weak_objects = {
        weak_root,
        {},
        {},
    };
    LunaStringBuilder weak_executable{};
    luna_string_builder_init(&weak_executable);
    EXPECT_TRUE(Link(weak_objects, 1U, &weak_executable, nullptr));
    luna_string_builder_destroy(&weak_executable);

    std::string negative_signed_root = weak_root;
    const std::optional<std::size_t> weak_relocation =
        FindFirstRelocation(negative_signed_root);
    ASSERT_TRUE(weak_relocation.has_value());
    const std::size_t weak_relocation_offset = weak_relocation.value_or(0U);
    StoreU32(negative_signed_root, weak_relocation_offset + 8U, 11U);
    StoreU64(negative_signed_root, weak_relocation_offset + 16U, UINT64_MAX);
    const std::array<std::string_view, 3U> negative_signed_objects = {
        negative_signed_root,
        {},
        {},
    };
    LunaStringBuilder negative_signed_executable{};
    luna_string_builder_init(&negative_signed_executable);
    EXPECT_TRUE(Link(negative_signed_objects, 1U, &negative_signed_executable,
                     nullptr));
    luna_string_builder_destroy(&negative_signed_executable);

    std::string weak_library = original_library;
    const std::optional<std::size_t> library_answer =
        FindSymbol(weak_library, "answer");
    ASSERT_TRUE(library_answer.has_value());
    const std::size_t library_answer_offset = library_answer.value_or(0U);
    const std::uint8_t library_type =
        static_cast<std::uint8_t>(weak_library[library_answer_offset + 4U]) &
        UINT8_C(0x0f);
    weak_library[library_answer_offset + 4U] = static_cast<char>(
        static_cast<std::uint8_t>(LUNA_TEST_ELF_SYMBOL_WEAK << 4U) |
        library_type);
    const std::array<std::string_view, 3U> override_objects = {
        original_root,
        weak_library,
        original_library,
    };
    LunaStringBuilder override_executable{};
    luna_string_builder_init(&override_executable);
    EXPECT_TRUE(Link(override_objects, 3U, &override_executable, nullptr));
    luna_string_builder_destroy(&override_executable);

    std::string overflow_root = original_root;
    const std::optional<std::size_t> relocation =
        FindFirstRelocation(overflow_root);
    ASSERT_TRUE(relocation.has_value());
    const std::size_t relocation_offset = relocation.value_or(0U);
    StoreU64(overflow_root, relocation_offset + 16U,
             UINT64_C(0x7fffffffffffffff));
    const std::array<std::string_view, 3U> overflow_objects = {
        overflow_root,
        original_library,
        {},
    };
    LunaStringBuilder overflow_executable{};
    luna_string_builder_init(&overflow_executable);
    EXPECT_FALSE(Link(overflow_objects, 2U, &overflow_executable, nullptr));
    luna_string_builder_destroy(&overflow_executable);
}

}
}
