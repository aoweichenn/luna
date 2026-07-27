#include "test_support.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

// 构建系统仅用这个宏传入真实源码根目录，运行时 ABI 不依赖预处理器。
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

[[nodiscard]] std::string ReadSource(std::string_view relative_path) {
    const std::filesystem::path source_path =
        std::filesystem::path{LUNA_TEST_SOURCE_ROOT} / relative_path;
    std::ifstream source{source_path, std::ios::binary};
    return {std::istreambuf_iterator<char>{source},
            std::istreambuf_iterator<char>{}};
}

TEST(FreestandingRuntimeTest, RealSourcesLowerToVerifiedTypedIr) {
    const std::string runtime_interface =
        ReadSource(LUNA_TEST_RUNTIME_INTERFACE_PATH);
    const std::string runtime_implementation =
        ReadSource(LUNA_TEST_RUNTIME_IMPLEMENTATION_PATH);
    const std::string syscall_interface =
        ReadSource(LUNA_TEST_SYSCALL_INTERFACE_PATH);
    const std::string syscall_implementation =
        ReadSource(LUNA_TEST_SYSCALL_IMPLEMENTATION_PATH);
    ASSERT_FALSE(runtime_interface.empty());
    ASSERT_FALSE(runtime_implementation.empty());
    ASSERT_FALSE(syscall_interface.empty());
    ASSERT_FALSE(syscall_implementation.empty());

    CompilationHarness harness{
        "module test.runtime_unit;\n"
        "import luna.runtime;\n"
        "fn main() -> i32 {\n"
        "    let file: RuntimeFile = runtime_standard_output();\n"
        "    let result: RuntimeIoResult = "
        "runtime_file_write(file, null, 0);\n"
        "    return result.error == RuntimeError.none ? 42 : 1;\n"
        "}\n",
        runtime_interface,
        runtime_implementation,
        syscall_interface,
        syscall_implementation,
    };

    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    ASSERT_TRUE(harness.EmitObject()) << harness.Diagnostics();
    EXPECT_FALSE(harness.Object().empty());
    EXPECT_NE(harness.Assembly().find("luna_linux_syscall"), std::string::npos);
}

TEST(FreestandingRuntimeTest, ResourceTypesRejectRawDescriptorArguments) {
    const std::string runtime_interface =
        ReadSource(LUNA_TEST_RUNTIME_INTERFACE_PATH);
    const std::string runtime_implementation =
        ReadSource(LUNA_TEST_RUNTIME_IMPLEMENTATION_PATH);
    const std::string syscall_interface =
        ReadSource(LUNA_TEST_SYSCALL_INTERFACE_PATH);
    const std::string syscall_implementation =
        ReadSource(LUNA_TEST_SYSCALL_IMPLEMENTATION_PATH);

    CompilationHarness harness{
        "module test.runtime_type_boundary;\n"
        "import luna.runtime;\n"
        "fn main() -> i32 {\n"
        "    let error: RuntimeError = runtime_file_close(1);\n"
        "    return error as isize as i32;\n"
        "}\n",
        runtime_interface,
        runtime_implementation,
        syscall_interface,
        syscall_implementation,
    };

    EXPECT_FALSE(harness.ParseAndLower());
    EXPECT_NE(harness.Diagnostics().find(
                  "aggregate initialization requires braces or an lvalue of "
                  "the exact same aggregate type"),
              std::string::npos)
        << harness.Diagnostics();
}

} // 匿名命名空间
} // 命名空间 luna::test
