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

}
