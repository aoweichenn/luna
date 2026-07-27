#include "test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace luna::test {
namespace {

constexpr std::size_t LUNA_TEST_ELF_TEXT_OFFSET_BYTES = 64U;
constexpr std::string_view LUNA_TEST_EXPECTED_SYSCALL_TEXT{
    "\x48\x89\xf8\x0f\x05\xc3"
    "\x48\x89\xf8\x48\x89\xf7\x0f\x05\xc3"
    "\x48\x89\xf8\x48\x89\xf7\x48\x89\xd6\x0f\x05\xc3"
    "\x48\x89\xf8\x48\x89\xf7\x48\x89\xd6\x48\x89\xca\x0f\x05\xc3"
    "\x48\x89\xf8\x48\x89\xf7\x48\x89\xd6\x48\x89\xca\x4d\x89\xc2"
    "\x0f\x05\xc3"
    "\x48\x89\xf8\x48\x89\xf7\x48\x89\xd6\x48\x89\xca\x4d\x89\xc2"
    "\x4d\x89\xc8\x0f\x05\xc3"
    "\x48\x89\xf8\x48\x89\xf7\x48\x89\xd6\x48\x89\xca\x4d\x89\xc2"
    "\x4d\x89\xc8\x4c\x8b\x4c\x24\x08\x0f\x05\xc3",
    107U,
};

class StringBuilderOwner final {
  public:
    StringBuilderOwner() {
        luna_string_builder_init(&this->builder_);
    }

    ~StringBuilderOwner() {
        luna_string_builder_destroy(&this->builder_);
    }

    StringBuilderOwner(const StringBuilderOwner &) = delete;
    StringBuilderOwner &operator=(const StringBuilderOwner &) = delete;
    StringBuilderOwner(StringBuilderOwner &&) = delete;
    StringBuilderOwner &operator=(StringBuilderOwner &&) = delete;

    [[nodiscard]] LunaStringBuilder *Get() noexcept {
        return &this->builder_;
    }

    [[nodiscard]] std::string Bytes() const {
        return {luna_string_builder_data(&this->builder_),
                this->builder_.length};
    }

  private:
    LunaStringBuilder builder_{};
};

[[nodiscard]] LunaStringView View(std::string_view bytes) {
    return {
        .data = bytes.data(),
        .length = bytes.size(),
    };
}

TEST(LinuxSyscallAbiTest, EmitsDeterministicSelfVerifiedObject) {
    StringBuilderOwner first;
    StringBuilderOwner second;
    ASSERT_TRUE(
        luna_x86_64_linux_syscall_abi_emit_object(nullptr, first.Get()));
    ASSERT_TRUE(
        luna_x86_64_linux_syscall_abi_emit_object(nullptr, second.Get()));

    const std::string first_bytes = first.Bytes();
    const std::string second_bytes = second.Bytes();
    ASSERT_FALSE(first_bytes.empty());
    EXPECT_EQ(first_bytes, second_bytes);
    EXPECT_TRUE(luna_x86_64_elf_object_verify(View(first_bytes), nullptr));
    EXPECT_TRUE(luna_x86_64_linux_syscall_abi_verify_object(View(first_bytes),
                                                            nullptr));
    ASSERT_GE(first_bytes.size(), LUNA_TEST_ELF_TEXT_OFFSET_BYTES +
                                      LUNA_TEST_EXPECTED_SYSCALL_TEXT.size());
    EXPECT_EQ(std::string_view{first_bytes}.substr(
                  LUNA_TEST_ELF_TEXT_OFFSET_BYTES,
                  LUNA_TEST_EXPECTED_SYSCALL_TEXT.size()),
              LUNA_TEST_EXPECTED_SYSCALL_TEXT);

    for (std::uint32_t argument_count = 0U;
         argument_count <= LUNA_X86_64_LINUX_SYSCALL_MAX_ARGUMENT_COUNT;
         argument_count += 1U) {
        const std::string symbol =
            "luna_linux_syscall" + std::to_string(argument_count);
        EXPECT_NE(first_bytes.find(symbol), std::string::npos);
    }
    EXPECT_EQ(LUNA_X86_64_LINUX_SYSCALL_MAX_ARGUMENT_COUNT, 6);
    EXPECT_EQ(LUNA_X86_64_LINUX_SYSCALL_MAX_ERRNO, 4095);
}

TEST(LinuxSyscallAbiTest, RejectsMutationAndInvalidOutputState) {
    StringBuilderOwner object;
    ASSERT_TRUE(
        luna_x86_64_linux_syscall_abi_emit_object(nullptr, object.Get()));
    std::string corrupted = object.Bytes();
    const std::string_view syscall_opcode{"\x0f\x05", 2U};
    const std::size_t syscall_offset = corrupted.find(syscall_opcode);
    ASSERT_NE(syscall_offset, std::string::npos);
    corrupted[syscall_offset] ^= static_cast<char>(UINT8_C(1));

    EXPECT_TRUE(luna_x86_64_elf_object_verify(View(corrupted), nullptr));
    EXPECT_FALSE(
        luna_x86_64_linux_syscall_abi_verify_object(View(corrupted), nullptr));
    EXPECT_FALSE(
        luna_x86_64_linux_syscall_abi_verify_object(LunaStringView{}, nullptr));

    StringBuilderOwner occupied;
    ASSERT_TRUE(
        luna_string_builder_append_c_string(occupied.Get(), "occupied"));
    EXPECT_FALSE(
        luna_x86_64_linux_syscall_abi_emit_object(nullptr, occupied.Get()));
    EXPECT_EQ(occupied.Get()->length, 0U);
}

} // 匿名命名空间
} // 命名空间 luna::test
