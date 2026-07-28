#include "test_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

// 构建系统只通过这个边界宏传入真实源码根目录。
#ifndef LUNA_TEST_SOURCE_ROOT
#error "LUNA_TEST_SOURCE_ROOT must identify the Luna source tree"
#endif

namespace luna::test {
namespace {

constexpr std::string_view LUNA_TEST_RUNTIME_INTERFACE_PATH =
    "runtime/luna/runtime.interface.luna";
constexpr std::string_view LUNA_TEST_RUNTIME_IMPLEMENTATION_PATH =
    "runtime/luna/runtime.luna";
constexpr std::string_view LUNA_TEST_SYSCALL_INTERFACE_PATH =
    "runtime/luna/linux/syscall.interface.luna";
constexpr std::string_view LUNA_TEST_SYSCALL_IMPLEMENTATION_PATH =
    "runtime/luna/linux/syscall.luna";
constexpr std::string_view LUNA_TEST_MEMORY_INTERFACE_PATH =
    "runtime/luna/std/memory.interface.luna";
constexpr std::string_view LUNA_TEST_MEMORY_IMPLEMENTATION_PATH =
    "runtime/luna/std/memory.luna";
constexpr std::string_view LUNA_TEST_BYTES_INTERFACE_PATH =
    "runtime/luna/std/bytes.interface.luna";
constexpr std::string_view LUNA_TEST_BYTES_IMPLEMENTATION_PATH =
    "runtime/luna/std/bytes.luna";
constexpr std::string_view LUNA_TEST_TEXT_INTERFACE_PATH =
    "runtime/luna/std/text.interface.luna";
constexpr std::string_view LUNA_TEST_TEXT_IMPLEMENTATION_PATH =
    "runtime/luna/std/text.luna";
constexpr std::string_view LUNA_TEST_LEXER_INTERFACE_PATH =
    "runtime/luna/bootstrap/frontend/lexer.interface.luna";
constexpr std::string_view LUNA_TEST_LEXER_IMPLEMENTATION_PATH =
    "runtime/luna/bootstrap/frontend/lexer.luna";
constexpr std::string_view LUNA_TEST_PARSER_INTERFACE_PATH =
    "runtime/luna/bootstrap/frontend/parser.interface.luna";
constexpr std::string_view LUNA_TEST_PARSER_IMPLEMENTATION_PATH =
    "runtime/luna/bootstrap/frontend/parser.luna";
constexpr std::string_view LUNA_TEST_TYPE_INTERFACE_PATH =
    "runtime/luna/bootstrap/middleend/type/type.interface.luna";
constexpr std::string_view LUNA_TEST_TYPE_IMPLEMENTATION_PATH =
    "runtime/luna/bootstrap/middleend/type/type.luna";
constexpr std::string_view LUNA_TEST_IR_INTERFACE_PATH =
    "runtime/luna/bootstrap/middleend/ir/ir.interface.luna";
constexpr std::string_view LUNA_TEST_IR_IMPLEMENTATION_PATH =
    "runtime/luna/bootstrap/middleend/ir/ir.luna";
constexpr std::string_view LUNA_TEST_SEMA_INTERFACE_PATH =
    "runtime/luna/bootstrap/middleend/sema/sema.interface.luna";
constexpr std::string_view LUNA_TEST_SEMA_IMPLEMENTATION_PATH =
    "runtime/luna/bootstrap/middleend/sema/sema.luna";

constexpr std::array<std::string_view, 6U> LUNA_TEST_MIDDLEEND_SOURCE_PATHS = {
    LUNA_TEST_TYPE_INTERFACE_PATH, LUNA_TEST_TYPE_IMPLEMENTATION_PATH,
    LUNA_TEST_IR_INTERFACE_PATH,   LUNA_TEST_IR_IMPLEMENTATION_PATH,
    LUNA_TEST_SEMA_INTERFACE_PATH, LUNA_TEST_SEMA_IMPLEMENTATION_PATH,
};

[[nodiscard]] std::string ReadSource(std::string_view relative_path) {
    const std::filesystem::path source_path =
        std::filesystem::path{LUNA_TEST_SOURCE_ROOT} / relative_path;
    std::ifstream source{source_path, std::ios::binary};
    return {std::istreambuf_iterator<char>{source},
            std::istreambuf_iterator<char>{}};
}

[[nodiscard]] CompilationHarness
MakeMiddleendHarness(std::string application_source) {
    return CompilationHarness{
        std::move(application_source),
        ReadSource(LUNA_TEST_SEMA_INTERFACE_PATH),
        ReadSource(LUNA_TEST_SEMA_IMPLEMENTATION_PATH),
        ReadSource(LUNA_TEST_IR_INTERFACE_PATH),
        ReadSource(LUNA_TEST_IR_IMPLEMENTATION_PATH),
        ReadSource(LUNA_TEST_TYPE_INTERFACE_PATH),
        ReadSource(LUNA_TEST_TYPE_IMPLEMENTATION_PATH),
        ReadSource(LUNA_TEST_PARSER_INTERFACE_PATH),
        ReadSource(LUNA_TEST_PARSER_IMPLEMENTATION_PATH),
        ReadSource(LUNA_TEST_LEXER_INTERFACE_PATH),
        ReadSource(LUNA_TEST_LEXER_IMPLEMENTATION_PATH),
        ReadSource(LUNA_TEST_TEXT_INTERFACE_PATH),
        ReadSource(LUNA_TEST_TEXT_IMPLEMENTATION_PATH),
        ReadSource(LUNA_TEST_BYTES_INTERFACE_PATH),
        ReadSource(LUNA_TEST_BYTES_IMPLEMENTATION_PATH),
        ReadSource(LUNA_TEST_MEMORY_INTERFACE_PATH),
        ReadSource(LUNA_TEST_MEMORY_IMPLEMENTATION_PATH),
        ReadSource(LUNA_TEST_RUNTIME_INTERFACE_PATH),
        ReadSource(LUNA_TEST_RUNTIME_IMPLEMENTATION_PATH),
        ReadSource(LUNA_TEST_SYSCALL_INTERFACE_PATH),
        ReadSource(LUNA_TEST_SYSCALL_IMPLEMENTATION_PATH),
    };
}

TEST(BootstrapMiddleendTest, RealSourcesLowerToVerifiedTypedIr) {
    CompilationHarness harness =
        MakeMiddleendHarness("module test.bootstrap_middleend;\n"
                             "import luna.runtime;\n"
                             "import luna.bootstrap.middleend.sema;\n"
                             "fn main() -> i32 {\n"
                             "    let input: BootstrapSemanticInput = "
                             "bootstrap_semantic_input_empty(true);\n"
                             "    let error: RuntimeError = "
                             "bootstrap_semantic_input_release(input);\n"
                             "    return error == RuntimeError.none ? 42 : 1;\n"
                             "}\n");

    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    ASSERT_TRUE(harness.EmitObject()) << harness.Diagnostics();
    EXPECT_FALSE(harness.Object().empty());
}

TEST(BootstrapMiddleendTest, PublicApiKeepsSyntaxInputStronglyTyped) {
    CompilationHarness harness =
        MakeMiddleendHarness("module test.bootstrap_middleend_type_error;\n"
                             "import luna.bootstrap.middleend.sema;\n"
                             "fn main() -> i32 {\n"
                             "    let result: BootstrapSemanticResult = "
                             "bootstrap_semantic_check(0);\n"
                             "    return result.error as isize as i32;\n"
                             "}\n");

    EXPECT_FALSE(harness.ParseAndLower());
    EXPECT_NE(
        harness.Diagnostics().find(
            "aggregate initialization requires braces or an lvalue of the "
            "exact same aggregate type"),
        std::string::npos)
        << harness.Diagnostics();
}

TEST(BootstrapMiddleendTest, ModulesKeepTheFreestandingBoundary) {
    for (const std::string_view source_path :
         LUNA_TEST_MIDDLEEND_SOURCE_PATHS) {
        const std::string source = ReadSource(source_path);
        ASSERT_FALSE(source.empty()) << source_path;
        EXPECT_EQ(source.find("luna.linux.syscall"), std::string::npos)
            << source_path;
        EXPECT_EQ(source.find("luna_linux_syscall"), std::string::npos)
            << source_path;
        EXPECT_EQ(source.find("extern "), std::string::npos) << source_path;
        EXPECT_EQ(source.find("libc"), std::string::npos) << source_path;
    }
}

} // 匿名命名空间
} // 命名空间 luna::test
