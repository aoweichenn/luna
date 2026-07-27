#include "test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

namespace luna::test {
namespace {

TEST(ModuleTest, LowersReachableAcyclicImportsWithExportVisibility) {
    CompilationHarness harness{
        "module app.main;\n"
        "import lib.math;\n"
        "import lib.core;\n"
        "fn main() -> i32 {\n"
        "    var value: Counter = { value = 20, };\n"
        "    return answer((&value) as *const Counter);\n"
        "}\n",
        "export module lib.core;\n"
        "export struct Counter { value: i32; }\n"
        "export fn increment(value: i32) -> i32;\n",
        "module lib.math;\n"
        "fn answer(input: *const Counter) -> i32 {\n"
        "    return increment(input->value) + 21;\n"
        "}\n",
        "export module lib.math;\n"
        "import lib.core;\n"
        "export fn answer(input: *const Counter) -> i32;\n",
        "module lib.core;\n"
        "fn increment(value: i32) -> i32 { return value + 1; }\n"};

    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    EXPECT_EQ(harness.Module()->functions.length, 3U);
    EXPECT_NE(harness.Module()->entry_function, LUNA_IR_INVALID_ID);
    EXPECT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
}

TEST(ModuleTest, RejectsInvalidDependencyGraphs) {
    {
        CompilationHarness harness{"module app.unknown;\n"
                                   "import missing.dependency;\n"
                                   "fn main() -> i32 { return 42; }\n"};
        EXPECT_FALSE(harness.ParseAndLower());
        EXPECT_NE(harness.Diagnostics().find("was not supplied"),
                  std::string::npos)
            << harness.Diagnostics();
    }
    {
        CompilationHarness harness{"module app.self;\n"
                                   "import app.self;\n"
                                   "fn main() -> i32 { return 42; }\n"};
        EXPECT_FALSE(harness.ParseAndLower());
        EXPECT_NE(harness.Diagnostics().find("cannot import itself"),
                  std::string::npos);
    }
    {
        CompilationHarness harness{"module cycle.left;\n"
                                   "import cycle.right;\n"
                                   "fn main() -> i32 { return 42; }\n",
                                   "export module cycle.right;\n"
                                   "import cycle.left;\n",
                                   "module cycle.right;\n",
                                   "export module cycle.left;\n"};
        EXPECT_FALSE(harness.ParseAndLower());
        EXPECT_NE(harness.Diagnostics().find("import cycle detected"),
                  std::string::npos);
    }
    {
        CompilationHarness harness{"module app.no_interface;\n"
                                   "import lib.hidden;\n"
                                   "fn main() -> i32 { return 42; }\n",
                                   "module lib.hidden;\n"
                                   "fn value() -> i32 { return 42; }\n"};
        EXPECT_FALSE(harness.ParseAndLower());
        EXPECT_NE(harness.Diagnostics().find("has no interface unit"),
                  std::string::npos);
    }
    {
        CompilationHarness harness{"module app.unreachable;\n"
                                   "fn main() -> i32 { return 42; }\n",
                                   "module lib.unused;\n"};
        EXPECT_FALSE(harness.ParseAndLower());
        EXPECT_NE(harness.Diagnostics().find("is not reachable"),
                  std::string::npos);
    }
    {
        CompilationHarness harness{"module app.multiple_root;\n"
                                   "fn main() -> i32 { return 42; }\n",
                                   "module lib.other_root;\n"
                                   "fn main() -> i32 { return 1; }\n"};
        EXPECT_FALSE(harness.ParseAndLower());
        EXPECT_NE(harness.Diagnostics().find(
                      "more than one module containing 'main'"),
                  std::string::npos);
    }
}

TEST(ModuleTest, EnforcesDirectExportVisibilityAndUnambiguousNames) {
    {
        CompilationHarness harness{"module app.private_access;\n"
                                   "import lib.private_access;\n"
                                   "fn main() -> i32 { return hidden(); }\n",
                                   "export module lib.private_access;\n"
                                   "fn hidden() -> i32;\n",
                                   "module lib.private_access;\n"
                                   "fn hidden() -> i32 { return 42; }\n"};
        EXPECT_FALSE(harness.ParseAndLower());
        EXPECT_NE(harness.Diagnostics().find("unknown function 'hidden'"),
                  std::string::npos);
    }
    {
        CompilationHarness harness{"module app.ambiguous;\n"
                                   "import lib.left;\n"
                                   "import lib.right;\n"
                                   "fn main() -> i32 { return value(); }\n",
                                   "export module lib.left;\n"
                                   "export fn value() -> i32;\n",
                                   "module lib.left;\n"
                                   "fn value() -> i32 { return 20; }\n",
                                   "export module lib.right;\n"
                                   "export fn value() -> i32;\n",
                                   "module lib.right;\n"
                                   "fn value() -> i32 { return 22; }\n"};
        EXPECT_FALSE(harness.ParseAndLower());
        EXPECT_NE(
            harness.Diagnostics().find("makes function 'value' ambiguous"),
            std::string::npos);
    }
    {
        CompilationHarness harness{
            "module app.private_type;\n"
            "import lib.private_type;\n"
            "fn main() -> i32 { return 42; }\n",
            "export module lib.private_type;\n"
            "struct Hidden { value: i32; }\n"
            "export fn read(value: *const Hidden) -> i32;\n",
            "module lib.private_type;\n"
            "fn read(value: *const Hidden) -> i32 { return value->value; }\n"};
        EXPECT_FALSE(harness.ParseAndLower());
        EXPECT_NE(
            harness.Diagnostics().find("exposes non-exported type 'Hidden'"),
            std::string::npos);
    }
    {
        CompilationHarness harness{
            "export module app.interface_scope;\n"
            "export fn read(value: *const Imported) -> i32;\n",
            "module app.interface_scope;\n"
            "import lib.types;\n"
            "fn read(value: *const Imported) -> i32 { return value->value; }\n"
            "fn main() -> i32 { return 42; }\n",
            "export module lib.types;\n"
            "export struct Imported { value: i32; }\n",
            "module lib.types;\n"};
        EXPECT_FALSE(harness.ParseAndLower());
        EXPECT_NE(harness.Diagnostics().find("unknown type 'Imported'"),
                  std::string::npos)
            << harness.Diagnostics();
    }
}

TEST(ModuleTest, DoesNotExposeTransitiveImports) {
    CompilationHarness harness{"module app.direct_visibility;\n"
                               "import lib.middle;\n"
                               "fn main() -> i32 { return leaf(); }\n",
                               "export module lib.middle;\n"
                               "import lib.leaf;\n"
                               "export fn middle() -> i32;\n",
                               "module lib.middle;\n"
                               "fn middle() -> i32 { return leaf(); }\n",
                               "export module lib.leaf;\n"
                               "export fn leaf() -> i32;\n",
                               "module lib.leaf;\n"
                               "fn leaf() -> i32 { return 42; }\n"};

    EXPECT_FALSE(harness.ParseAndLower());
    EXPECT_NE(harness.Diagnostics().find("unknown function 'leaf'"),
              std::string::npos)
        << harness.Diagnostics();
}

TEST(ModuleTest, DeduplicatesCompatibleExternalSymbolsAcrossModules) {
    CompilationHarness harness{
        "module app.external_symbols;\n"
        "import lib.external_left;\n"
        "import lib.external_right;\n"
        "fn main() -> i32 { return left(20) + right(22); }\n",
        "export module lib.external_left;\n"
        "export fn left(value: i32) -> i32;\n",
        "module lib.external_left;\n"
        "extern fn c_identity(value: i32) -> i32;\n"
        "fn left(value: i32) -> i32 { return c_identity(value); }\n",
        "export module lib.external_right;\n"
        "export fn right(value: i32) -> i32;\n",
        "module lib.external_right;\n"
        "extern fn c_identity(value: i32) -> i32;\n"
        "fn right(value: i32) -> i32 { return c_identity(value); }\n"};

    ASSERT_TRUE(harness.Verify()) << harness.Diagnostics();
    EXPECT_EQ(harness.Module()->functions.length, 4U);

    std::size_t external_count = 0U;
    for (std::size_t index = 0U; index < harness.Module()->functions.length;
         index += 1U) {
        const auto *function = static_cast<const LunaIrFunction *>(
            luna_vector_at_const(&harness.Module()->functions, index));
        ASSERT_NE(function, nullptr);
        if (function->linkage == LUNA_IR_LINKAGE_EXTERNAL_C) {
            external_count += 1U;
        }
    }
    EXPECT_EQ(external_count, 1U);
    EXPECT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
}

TEST(ModuleTest, RejectsConflictingExternalSymbolsAcrossModules) {
    CompilationHarness harness{
        "module app.external_conflict;\n"
        "import lib.external_i32;\n"
        "import lib.external_i64;\n"
        "fn main() -> i32 { return value_i32(42) + value_i64(0); }\n",
        "export module lib.external_i32;\n"
        "export fn value_i32(value: i32) -> i32;\n",
        "module lib.external_i32;\n"
        "extern fn c_identity(value: i32) -> i32;\n"
        "fn value_i32(value: i32) -> i32 { return c_identity(value); }\n",
        "export module lib.external_i64;\n"
        "export fn value_i64(value: i32) -> i32;\n",
        "module lib.external_i64;\n"
        "extern fn c_identity(value: i64) -> i64;\n"
        "fn value_i64(value: i32) -> i32 {\n"
        "    return c_identity(value as i64) as i32;\n"
        "}\n"};

    EXPECT_FALSE(harness.ParseAndLower());
    EXPECT_NE(harness.Diagnostics().find(
                  "external symbol 'c_identity' has a conflicting "
                  "declaration"),
              std::string::npos)
        << harness.Diagnostics();
}

}
}
