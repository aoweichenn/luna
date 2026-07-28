#include "test_support.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

// 构建系统仅通过这个宏传入真实源码根目录。
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

[[nodiscard]] std::string ReadSource(std::string_view relative_path) {
    const std::filesystem::path source_path =
        std::filesystem::path{LUNA_TEST_SOURCE_ROOT} / relative_path;
    std::ifstream source{source_path, std::ios::binary};
    return {std::istreambuf_iterator<char>{source},
            std::istreambuf_iterator<char>{}};
}

[[nodiscard]] CompilationHarness
MakeBootstrapHarness(std::string application_source) {
    return CompilationHarness{
        std::move(application_source),
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

TEST(BootstrapFrontendTest, RealSourcesLowerToVerifiedTypedIr) {
    CompilationHarness harness = MakeBootstrapHarness(
        "module test.bootstrap_frontend;\n"
        "import luna.runtime;\n"
        "import luna.std.text;\n"
        "import luna.bootstrap.frontend.lexer;\n"
        "import luna.bootstrap.frontend.parser;\n"
        "fn main() -> i32 {\n"
        "    let source: StdTextViewResult = std_text_from_c_string(\n"
        "        \"module sample; fn main() -> i32 { return 42; }\", 64\n"
        "    );\n"
        "    let result: BootstrapFrontendResult =\n"
        "        bootstrap_frontend_parse(source.view);\n"
        "    let valid: bool = result.error == RuntimeError.none &&\n"
        "        bootstrap_syntax_tree_is_valid(result.parse.tree);\n"
        "    let released: RuntimeError =\n"
        "        bootstrap_frontend_result_release(result);\n"
        "    return valid && released == RuntimeError.none ? 42 : 1;\n"
        "}\n");

    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    ASSERT_TRUE(harness.EmitObject()) << harness.Diagnostics();
    EXPECT_FALSE(harness.Object().empty());
}

TEST(BootstrapFrontendTest, StrongTypesRejectUntypedTokenStorage) {
    CompilationHarness harness = MakeBootstrapHarness(
        "module test.bootstrap_frontend_type_error;\n"
        "import luna.std.text;\n"
        "import luna.bootstrap.frontend.parser;\n"
        "fn main() -> i32 {\n"
        "    let source: StdTextViewResult =\n"
        "        std_text_from_c_string(\"module sample;\", 16);\n"
        "    let result: BootstrapParseResult =\n"
        "        bootstrap_parse(source.view, 0);\n"
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

TEST(BootstrapFrontendTest, ModulesRespectFreestandingBoundary) {
    for (const std::string_view source_path :
         {LUNA_TEST_LEXER_INTERFACE_PATH, LUNA_TEST_LEXER_IMPLEMENTATION_PATH,
          LUNA_TEST_PARSER_INTERFACE_PATH,
          LUNA_TEST_PARSER_IMPLEMENTATION_PATH}) {
        const std::string source = ReadSource(source_path);
        ASSERT_FALSE(source.empty()) << source_path;
        EXPECT_EQ(source.find("luna.linux.syscall"), std::string::npos)
            << source_path;
        EXPECT_EQ(source.find("luna_linux_syscall"), std::string::npos)
            << source_path;
        EXPECT_EQ(source.find("extern "), std::string::npos) << source_path;
    }
}

} // 匿名命名空间
} // 命名空间 luna::test
