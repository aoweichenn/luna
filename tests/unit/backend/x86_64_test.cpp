#include "test_support.hpp"

#include <gtest/gtest.h>

#include <string>

namespace luna::test {

TEST(X8664BackendTest, EmitsDirectAssemblyForTypedIr) {
    FrontendHarness harness{"module test.codegen;\n"
                            "fn calculate(left: i32, right: i32) -> i32 {\n"
                            "    return (left * right) + (left >> 1);\n"
                            "}\n"
                            "fn main() -> i32 { return calculate(6, 7); }\n"};

    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find(".globl _start"), std::string::npos);
    EXPECT_NE(assembly.find("imull"), std::string::npos);
    EXPECT_NE(assembly.find("sarl %cl, %eax"), std::string::npos);
    EXPECT_NE(assembly.find("call _L"), std::string::npos);
    EXPECT_NE(assembly.find(".note.GNU-stack"), std::string::npos);
}

TEST(X8664BackendTest, EmitsShortCircuitControlFlowAsBranches) {
    FrontendHarness harness{"module test.short_circuit_codegen;\n"
                            "fn main() -> i32 {\n"
                            "    if (false && (1 / 0 == 0)) { return 1; }\n"
                            "    return 42;\n"
                            "}\n"};

    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find("jne .Lfn0_bb"), std::string::npos);
    EXPECT_NE(assembly.find("idivl"), std::string::npos);
}

TEST(X8664BackendTest, EmitsI64InstructionsAndCallingConvention) {
    FrontendHarness harness{
        "module test.i64_codegen;\n"
        "fn calculate(left: i64, right: i64) -> i64 {\n"
        "    return ((left * right) + left) >> 2;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    if (calculate(4294967296, 7) == 8589934592) { return 42; }\n"
        "    return 1;\n"
        "}\n"};

    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find("movq %rdi"), std::string::npos);
    EXPECT_NE(assembly.find("movabsq $4294967296"), std::string::npos);
    EXPECT_NE(assembly.find("imulq"), std::string::npos);
    EXPECT_NE(assembly.find("sarq %cl, %rax"), std::string::npos);
    EXPECT_NE(assembly.find("cmpq"), std::string::npos);
}

TEST(X8664BackendTest, EmitsIntegerExtensionAndTruncation) {
    FrontendHarness harness{"module test.conversion_codegen;\n"
                            "fn convert(value: i32) -> i32 {\n"
                            "    return (value as i64) as i32;\n"
                            "}\n"
                            "fn main() -> i32 { return convert(-1); }\n"};

    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find("movslq"), std::string::npos);
    EXPECT_NE(assembly.find("movl"), std::string::npos);
}

}
