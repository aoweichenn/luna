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
    EXPECT_NE(assembly.find("movabsq $0x0000000100000000"), std::string::npos);
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

TEST(X8664BackendTest, EmitsUnsignedInstructionsAndConversions) {
    FrontendHarness harness{
        "module test.unsigned_codegen;\n"
        "fn calculate32(value: u32) -> u32 {\n"
        "    return (value / 3) + (value % 5) + (value >> 31);\n"
        "}\n"
        "fn calculate64(value: u64) -> u64 {\n"
        "    return (value / 7) + (value >> 63);\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    let narrow: u32 = calculate32(4294967295);\n"
        "    let wide: u64 = calculate64(narrow as u64);\n"
        "    if (narrow > 1 && wide >= 1) { return 42; }\n"
        "    return 1;\n"
        "}\n"};

    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find("divl"), std::string::npos);
    EXPECT_NE(assembly.find("divq"), std::string::npos);
    EXPECT_NE(assembly.find("shrl %cl, %eax"), std::string::npos);
    EXPECT_NE(assembly.find("shrq %cl, %rax"), std::string::npos);
    EXPECT_NE(assembly.find("seta %al"), std::string::npos);
    EXPECT_NE(assembly.find("setae %al"), std::string::npos);
    EXPECT_NE(assembly.find("xorl %edx, %edx"), std::string::npos);
    EXPECT_NE(assembly.find("xorq %rdx, %rdx"), std::string::npos);
}

TEST(X8664BackendTest, EmitsCompleteNarrowIntegerSemantics) {
    FrontendHarness harness{
        "module test.narrow_codegen;\n"
        "fn signed_byte(left: i8, right: i8) -> i8 {\n"
        "    let quotient: i8 = left / right;\n"
        "    let shifted: i8 = left >> right;\n"
        "    if (left < right) { return quotient + shifted; }\n"
        "    return quotient - shifted;\n"
        "}\n"
        "fn signed_word(left: i16, right: i16) -> i16 {\n"
        "    return (left % right) >> right;\n"
        "}\n"
        "fn unsigned_byte(left: u8, right: u8) -> u8 {\n"
        "    if (left > right) { return (left / right) >> right; }\n"
        "    return left;\n"
        "}\n"
        "fn unsigned_word(left: u16, right: u16) -> u16 {\n"
        "    return (left % right) << right;\n"
        "}\n"
        "fn widen_byte(value: i8) -> i64 { return value as i64; }\n"
        "fn widen_word(value: i16) -> i64 { return value as i64; }\n"
        "fn truncate_byte(value: u64) -> u8 { return value as u8; }\n"
        "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find("movsbl"), std::string::npos);
    EXPECT_NE(assembly.find("movswl"), std::string::npos);
    EXPECT_NE(assembly.find("movsbq"), std::string::npos);
    EXPECT_NE(assembly.find("movswq"), std::string::npos);
    EXPECT_NE(assembly.find("andl $255, %eax"), std::string::npos);
    EXPECT_NE(assembly.find("andl $65535, %eax"), std::string::npos);
    EXPECT_NE(assembly.find("andl $7, %ecx"), std::string::npos);
    EXPECT_NE(assembly.find("andl $15, %ecx"), std::string::npos);
    EXPECT_NE(assembly.find("idivl %ecx"), std::string::npos);
    EXPECT_NE(assembly.find("divl %ecx"), std::string::npos);
    EXPECT_NE(assembly.find("setl %al"), std::string::npos);
    EXPECT_NE(assembly.find("seta %al"), std::string::npos);
    EXPECT_NE(assembly.find("_safe:"), std::string::npos);
}

}
