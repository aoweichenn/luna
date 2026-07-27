#include "test_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

// 构建系统仅用这个宏传入真实源码根目录，标准库本身不依赖预处理器。
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
constexpr std::array<std::string_view, 5U> LUNA_TEST_STANDARD_MODULE_NAMES = {
    "memory", "bytes", "text", "path", "io",
};

[[nodiscard]] std::string ReadSource(std::string_view relative_path) {
    const std::filesystem::path source_path =
        std::filesystem::path{LUNA_TEST_SOURCE_ROOT} / relative_path;
    std::ifstream source{source_path, std::ios::binary};
    return {std::istreambuf_iterator<char>{source},
            std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::string ReadStandardSource(std::string_view module_name,
                                             bool is_interface) {
    std::string relative_path{"runtime/luna/std/"};
    relative_path.append(module_name);
    relative_path.append(is_interface ? ".interface.luna" : ".luna");
    return ReadSource(relative_path);
}

TEST(MinimumStandardLibraryTest, RealSourcesLowerToVerifiedTypedIr) {
    const std::string runtime_interface =
        ReadSource(LUNA_TEST_RUNTIME_INTERFACE_PATH);
    const std::string runtime_implementation =
        ReadSource(LUNA_TEST_RUNTIME_IMPLEMENTATION_PATH);
    const std::string syscall_interface =
        ReadSource(LUNA_TEST_SYSCALL_INTERFACE_PATH);
    const std::string syscall_implementation =
        ReadSource(LUNA_TEST_SYSCALL_IMPLEMENTATION_PATH);
    const std::string memory_interface = ReadStandardSource("memory", true);
    const std::string memory_implementation =
        ReadStandardSource("memory", false);
    const std::string bytes_interface = ReadStandardSource("bytes", true);
    const std::string bytes_implementation = ReadStandardSource("bytes", false);
    const std::string text_interface = ReadStandardSource("text", true);
    const std::string text_implementation = ReadStandardSource("text", false);
    const std::string path_interface = ReadStandardSource("path", true);
    const std::string path_implementation = ReadStandardSource("path", false);
    const std::string io_interface = ReadStandardSource("io", true);
    const std::string io_implementation = ReadStandardSource("io", false);

    CompilationHarness harness{
        "module test.minimum_standard_library;\n"
        "import luna.runtime;\n"
        "import luna.std.memory;\n"
        "import luna.std.bytes;\n"
        "import luna.std.text;\n"
        "import luna.std.path;\n"
        "import luna.std.io;\n"
        "fn main() -> i32 {\n"
        "    let view: StdTextViewResult = "
        "std_text_from_c_string(\"unit\", 5);\n"
        "    let path: StdPathResult = "
        "std_path_from_text(view.view);\n"
        "    let buffer: StdByteBufferResult = "
        "std_byte_buffer_from(\"ok\", 2);\n"
        "    return view.error == RuntimeError.none && "
        "path.error == RuntimeError.none && "
        "buffer.error == RuntimeError.none ? 42 : 1;\n"
        "}\n",
        io_interface,
        io_implementation,
        path_interface,
        path_implementation,
        text_interface,
        text_implementation,
        bytes_interface,
        bytes_implementation,
        memory_interface,
        memory_implementation,
        runtime_interface,
        runtime_implementation,
        syscall_interface,
        syscall_implementation,
    };

    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    ASSERT_TRUE(harness.EmitObject()) << harness.Diagnostics();
    EXPECT_FALSE(harness.Object().empty());
}

TEST(MinimumStandardLibraryTest, ResourceTypesRejectUntypedArguments) {
    const std::string runtime_interface =
        ReadSource(LUNA_TEST_RUNTIME_INTERFACE_PATH);
    const std::string runtime_implementation =
        ReadSource(LUNA_TEST_RUNTIME_IMPLEMENTATION_PATH);
    const std::string syscall_interface =
        ReadSource(LUNA_TEST_SYSCALL_INTERFACE_PATH);
    const std::string syscall_implementation =
        ReadSource(LUNA_TEST_SYSCALL_IMPLEMENTATION_PATH);
    const std::string memory_interface = ReadStandardSource("memory", true);
    const std::string memory_implementation =
        ReadStandardSource("memory", false);
    const std::string bytes_interface = ReadStandardSource("bytes", true);
    const std::string bytes_implementation = ReadStandardSource("bytes", false);

    CompilationHarness harness{
        "module test.standard_library_type_boundary;\n"
        "import luna.std.bytes;\n"
        "fn main() -> i32 {\n"
        "    let result: StdByteBufferResult = "
        "std_byte_buffer_push(0, 1);\n"
        "    return result.error as isize as i32;\n"
        "}\n",
        bytes_interface,
        bytes_implementation,
        memory_interface,
        memory_implementation,
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

TEST(MinimumStandardLibraryTest, ModulesDoNotBypassFreestandingRuntime) {
    for (const std::string_view module_name : LUNA_TEST_STANDARD_MODULE_NAMES) {
        const std::string interface_source =
            ReadStandardSource(module_name, true);
        const std::string implementation_source =
            ReadStandardSource(module_name, false);
        ASSERT_FALSE(interface_source.empty()) << module_name;
        ASSERT_FALSE(implementation_source.empty()) << module_name;
        EXPECT_EQ(interface_source.find("luna.linux.syscall"),
                  std::string::npos)
            << module_name;
        EXPECT_EQ(implementation_source.find("luna.linux.syscall"),
                  std::string::npos)
            << module_name;
        EXPECT_EQ(implementation_source.find("luna_linux_syscall"),
                  std::string::npos)
            << module_name;
    }
}

} // 匿名命名空间
} // 命名空间 luna::test
