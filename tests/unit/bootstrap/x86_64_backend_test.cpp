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
constexpr std::string_view LUNA_TEST_BACKEND_TEXT_INTERFACE_PATH =
    "runtime/luna/bootstrap/backend/x86_64/text/text.interface.luna";
constexpr std::string_view LUNA_TEST_BACKEND_TEXT_IMPLEMENTATION_PATH =
    "runtime/luna/bootstrap/backend/x86_64/text/text.luna";
constexpr std::string_view LUNA_TEST_BACKEND_ABI_INTERFACE_PATH =
    "runtime/luna/bootstrap/backend/x86_64/abi/abi.interface.luna";
constexpr std::string_view LUNA_TEST_BACKEND_ABI_IMPLEMENTATION_PATH =
    "runtime/luna/bootstrap/backend/x86_64/abi/abi.luna";
constexpr std::string_view LUNA_TEST_BACKEND_FRAME_INTERFACE_PATH =
    "runtime/luna/bootstrap/backend/x86_64/frame/frame.interface.luna";
constexpr std::string_view LUNA_TEST_BACKEND_FRAME_IMPLEMENTATION_PATH =
    "runtime/luna/bootstrap/backend/x86_64/frame/frame.luna";
constexpr std::string_view LUNA_TEST_BACKEND_CODEGEN_INTERFACE_PATH =
    "runtime/luna/bootstrap/backend/x86_64/codegen/codegen.interface.luna";
constexpr std::string_view LUNA_TEST_BACKEND_CODEGEN_IMPLEMENTATION_PATH =
    "runtime/luna/bootstrap/backend/x86_64/codegen/codegen.luna";

constexpr std::array LUNA_TEST_BACKEND_SOURCE_PATHS = {
    LUNA_TEST_BACKEND_TEXT_INTERFACE_PATH,
    LUNA_TEST_BACKEND_TEXT_IMPLEMENTATION_PATH,
    LUNA_TEST_BACKEND_ABI_INTERFACE_PATH,
    LUNA_TEST_BACKEND_ABI_IMPLEMENTATION_PATH,
    LUNA_TEST_BACKEND_FRAME_INTERFACE_PATH,
    LUNA_TEST_BACKEND_FRAME_IMPLEMENTATION_PATH,
    LUNA_TEST_BACKEND_CODEGEN_INTERFACE_PATH,
    LUNA_TEST_BACKEND_CODEGEN_IMPLEMENTATION_PATH,
};

[[nodiscard]] std::string ReadSource(std::string_view relative_path) {
    const std::filesystem::path source_path =
        std::filesystem::path{LUNA_TEST_SOURCE_ROOT} / relative_path;
    std::ifstream source{source_path, std::ios::binary};
    return {std::istreambuf_iterator<char>{source},
            std::istreambuf_iterator<char>{}};
}

[[nodiscard]] CompilationHarness
MakeBackendHarness(std::string application_source) {
    return CompilationHarness{
        std::move(application_source),
        ReadSource(LUNA_TEST_BACKEND_CODEGEN_INTERFACE_PATH),
        ReadSource(LUNA_TEST_BACKEND_CODEGEN_IMPLEMENTATION_PATH),
        ReadSource(LUNA_TEST_BACKEND_FRAME_INTERFACE_PATH),
        ReadSource(LUNA_TEST_BACKEND_FRAME_IMPLEMENTATION_PATH),
        ReadSource(LUNA_TEST_BACKEND_ABI_INTERFACE_PATH),
        ReadSource(LUNA_TEST_BACKEND_ABI_IMPLEMENTATION_PATH),
        ReadSource(LUNA_TEST_BACKEND_TEXT_INTERFACE_PATH),
        ReadSource(LUNA_TEST_BACKEND_TEXT_IMPLEMENTATION_PATH),
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

TEST(BootstrapX8664BackendTest, RealSourcesCompileToNativeObject) {
    CompilationHarness harness =
        MakeBackendHarness("module test.bootstrap_x86_64_backend;\n"
                           "import luna.runtime;\n"
                           "import luna.bootstrap.backend.x86_64.codegen;\n"
                           "fn main() -> i32 {\n"
                           "    let result: BootstrapX8664BackendResult =\n"
                           "        bootstrap_x86_64_backend_empty();\n"
                           "    let error: RuntimeError =\n"
                           "        bootstrap_x86_64_backend_release(result);\n"
                           "    return error == RuntimeError.none ? 42 : 1;\n"
                           "}\n");

    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    ASSERT_TRUE(harness.EmitObject()) << harness.Diagnostics();
    EXPECT_FALSE(harness.Object().empty());
}

TEST(BootstrapX8664BackendTest, PublicApiRequiresVerifiedTypedInputs) {
    CompilationHarness harness =
        MakeBackendHarness("module test.bootstrap_x86_64_backend_type_error;\n"
                           "import luna.bootstrap.backend.x86_64.codegen;\n"
                           "fn main() -> i32 {\n"
                           "    let result: BootstrapX8664BackendResult =\n"
                           "        bootstrap_x86_64_emit_assembly(0, 0, 0);\n"
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

TEST(BootstrapX8664BackendTest, SourcesKeepTheFreestandingBoundary) {
    for (const std::string_view source_path : LUNA_TEST_BACKEND_SOURCE_PATHS) {
        const std::string source = ReadSource(source_path);
        ASSERT_FALSE(source.empty()) << source_path;
        EXPECT_EQ(source.find("luna.linux.syscall"), std::string::npos)
            << source_path;
        EXPECT_EQ(source.find("luna_linux_syscall"), std::string::npos)
            << source_path;
        EXPECT_EQ(source.find("\nextern "), std::string::npos) << source_path;
        EXPECT_EQ(source.find("libc"), std::string::npos) << source_path;
    }
}

TEST(BootstrapX8664BackendTest, PlansAreRecomputedBeforeCodeGeneration) {
    const std::string abi =
        ReadSource(LUNA_TEST_BACKEND_ABI_IMPLEMENTATION_PATH);
    const std::string frame =
        ReadSource(LUNA_TEST_BACKEND_FRAME_IMPLEMENTATION_PATH);
    const std::string codegen =
        ReadSource(LUNA_TEST_BACKEND_CODEGEN_IMPLEMENTATION_PATH);

    EXPECT_NE(abi.find("bootstrap_x86_64_abi_equal"), std::string::npos);
    EXPECT_NE(frame.find("bootstrap_x86_64_frame_plan_equal"),
              std::string::npos);
    EXPECT_NE(codegen.find("bootstrap_x86_64_abi_is_valid"), std::string::npos);
    EXPECT_NE(codegen.find("bootstrap_x86_64_frame_plan_is_valid"),
              std::string::npos);
}

} // 匿名命名空间
} // 命名空间 luna::test
