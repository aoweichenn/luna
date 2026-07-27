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

TEST(X8664BackendTest, EmitsRawExternalSymbolsAndCAbiBoundaryFixups) {
    FrontendHarness harness{
        "module test.external_codegen;\n"
        "extern fn c_ready(value: i8) -> bool;\n"
        "fn main() -> i32 { return c_ready(-1) ? 42 : 1; }\n"};

    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find(".extern c_ready"), std::string::npos);
    EXPECT_NE(assembly.find("call c_ready"), std::string::npos);
    EXPECT_NE(assembly.find("movsbl"), std::string::npos);
    EXPECT_NE(assembly.find(", %edi"), std::string::npos);
    EXPECT_NE(assembly.find("testb %al, %al"), std::string::npos);
    EXPECT_EQ(assembly.find(".type c_ready, @function"), std::string::npos);
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

TEST(X8664BackendTest, EmitsScalarStackArgumentsForBothCallBoundaries) {
    FrontendHarness harness{
        "module test.stack_argument_codegen;\n"
        "fn select(a: i32, b: i32, c: i32, d: i32, e: i32, f: i32,\n"
        "          g: i64, x: f64, y: f64, z: f64, p: f64, q: f64,\n"
        "          r: f64, s: f64, t: f64, u: f64) -> i64 {\n"
        "    return g + (u as i64);\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    return select(0, 0, 0, 0, 0, 0, 40,\n"
        "                  0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,\n"
        "                  2.0) as i32;\n"
        "}\n"};

    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find("subq $16, %rsp"), std::string::npos);
    EXPECT_NE(assembly.find("movq %rax, 0(%rsp)"), std::string::npos);
    EXPECT_NE(assembly.find("movsd %xmm15, 8(%rsp)"), std::string::npos);
    EXPECT_NE(assembly.find("movq 16(%rbp), %rax"), std::string::npos);
    EXPECT_NE(assembly.find("movsd 24(%rbp), %xmm15"), std::string::npos);
    EXPECT_NE(assembly.find("addq $16, %rsp"), std::string::npos);
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

TEST(X8664BackendTest, EmitsPointerSizedIntegerSemanticsAndAbi) {
    FrontendHarness harness{
        "module test.pointer_sized_codegen;\n"
        "fn signed_value(left: isize, right: isize) -> isize {\n"
        "    let quotient: isize = left / right;\n"
        "    if (left < right) { return quotient >> right; }\n"
        "    return quotient;\n"
        "}\n"
        "fn unsigned_value(left: usize, right: usize) -> usize {\n"
        "    let quotient: usize = left / right;\n"
        "    if (left > right) { return quotient >> right; }\n"
        "    return quotient;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    let signed: isize = signed_value(-64, 2);\n"
        "    let unsigned: usize = unsigned_value(64, 2);\n"
        "    let signed_bits: i64 = signed as i64;\n"
        "    let unsigned_bits: u64 = unsigned as u64;\n"
        "    if (signed_bits < 0 && unsigned_bits > 0) { return 42; }\n"
        "    return 1;\n"
        "}\n"};

    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find("movq %rdi"), std::string::npos);
    EXPECT_NE(assembly.find("idivq"), std::string::npos);
    EXPECT_NE(assembly.find("divq"), std::string::npos);
    EXPECT_NE(assembly.find("sarq %cl, %rax"), std::string::npos);
    EXPECT_NE(assembly.find("shrq %cl, %rax"), std::string::npos);
    EXPECT_NE(assembly.find("setl %al"), std::string::npos);
    EXPECT_NE(assembly.find("seta %al"), std::string::npos);
}

TEST(X8664BackendTest, EmitsFloatingPointSemanticsAndSystemVAbi) {
    FrontendHarness harness{
        "module test.float_codegen;\n"
        "fn calculate(a: f32, b: f64, c: f32, d: f64,\n"
        "             e: f32, f: f64, g: f32, h: f64) -> f64 {\n"
        "    let single: f32 = (a + c) * e / g;\n"
        "    let double: f64 = (b - d) + f / h;\n"
        "    if (single <= 10.0 && double != 0.0) { return -double; }\n"
        "    return double;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    if (calculate(1.0, 2.0, 3.0, 4.0,\n"
        "                  5.0, 6.0, 7.0, 8.0) < 0.0) { return 42; }\n"
        "    return 1;\n"
        "}\n"};

    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find("ldmxcsr"), std::string::npos);
    EXPECT_NE(assembly.find("movss %xmm0"), std::string::npos);
    EXPECT_NE(assembly.find("movsd %xmm1"), std::string::npos);
    EXPECT_NE(assembly.find("%xmm7"), std::string::npos);
    EXPECT_NE(assembly.find("addss"), std::string::npos);
    EXPECT_NE(assembly.find("mulss"), std::string::npos);
    EXPECT_NE(assembly.find("divss"), std::string::npos);
    EXPECT_NE(assembly.find("subsd"), std::string::npos);
    EXPECT_NE(assembly.find("divsd"), std::string::npos);
    EXPECT_NE(assembly.find("ucomiss"), std::string::npos);
    EXPECT_NE(assembly.find("ucomisd"), std::string::npos);
    EXPECT_NE(assembly.find("setp %cl"), std::string::npos);
    EXPECT_NE(assembly.find("movabsq $0x8000000000000000"), std::string::npos);
}

TEST(X8664BackendTest, EmitsCheckedScalarConversions) {
    FrontendHarness harness{
        "module test.scalar_conversion_codegen;\n"
        "fn widen(value: f32) -> f64 { return value as f64; }\n"
        "fn narrow(value: f64) -> f32 { return value as f32; }\n"
        "fn signed_to_float(value: i64) -> f64 { return value as f64; }\n"
        "fn unsigned_to_float(value: u64) -> f32 { return value as f32; }\n"
        "fn float_to_signed(value: f64) -> i64 { return value as i64; }\n"
        "fn float_to_unsigned(value: f32) -> u64 { return value as u64; }\n"
        "fn main() -> i32 { return 0; }\n"};

    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find("cvtss2sd"), std::string::npos);
    EXPECT_NE(assembly.find("cvtsd2ss"), std::string::npos);
    EXPECT_NE(assembly.find("cvtsi2sdq"), std::string::npos);
    EXPECT_NE(assembly.find("cvtsi2ssq"), std::string::npos);
    EXPECT_NE(assembly.find("cvttsd2siq"), std::string::npos);
    EXPECT_NE(assembly.find("cvttss2siq"), std::string::npos);
    EXPECT_NE(assembly.find("ucomisd"), std::string::npos);
    EXPECT_NE(assembly.find("ucomiss"), std::string::npos);
    EXPECT_NE(assembly.find("_uitofp"), std::string::npos);
    EXPECT_NE(assembly.find("_fptoi"), std::string::npos);
    EXPECT_NE(assembly.find("ud2"), std::string::npos);
}

TEST(X8664BackendTest, EmitsTypedMemoryAndReadOnlyGlobalData) {
    FrontendHarness harness{"module test.memory_codegen;\n"
                            "fn main() -> i32 {\n"
                            "    var matrix: [2][3]i16 = {};\n"
                            "    matrix[1][2] = 42;\n"
                            "    let pointer: *i16 = &matrix[0][0];\n"
                            "    pointer[1] = matrix[1][2];\n"
                            "    var values: [2]f32 = {};\n"
                            "    values[1] = 1.5;\n"
                            "    let text: *const u8 = \"A\";\n"
                            "    if (pointer != null && text[0] == 65) {\n"
                            "        return pointer[1] as i32;\n"
                            "    }\n"
                            "    return 0;\n"
                            "}\n"};

    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find(".section .rodata"), std::string::npos);
    EXPECT_NE(assembly.find(".Lglobal0"), std::string::npos);
    EXPECT_NE(assembly.find(".byte 0x41"), std::string::npos);
    EXPECT_NE(assembly.find("rep stosb"), std::string::npos);
    EXPECT_NE(assembly.find("leaq"), std::string::npos);
    EXPECT_NE(assembly.find("imulq $6"), std::string::npos);
    EXPECT_NE(assembly.find("movw %cx, (%rax)"), std::string::npos);
    EXPECT_NE(assembly.find("movzwl (%rax), %eax"), std::string::npos);
    EXPECT_NE(assembly.find("movss %xmm0, (%rax)"), std::string::npos);
    EXPECT_NE(assembly.find("cmpq $"), std::string::npos);
    EXPECT_NE(assembly.find("testq %rax, %rax"), std::string::npos);
    EXPECT_NE(assembly.find("ud2"), std::string::npos);
}

TEST(X8664BackendTest, EmitsDirectAggregateMemberAddressing) {
    FrontendHarness harness{"module test.aggregate_codegen;\n"
                            "enum Kind: u8 { ready = 7, }\n"
                            "struct Inner { byte: u8; value: i32; }\n"
                            "struct Outer { kind: Kind; inner: Inner; }\n"
                            "fn main() -> i32 {\n"
                            "    var outer: Outer = {};\n"
                            "    let pointer: *Outer = &outer;\n"
                            "    pointer->kind = Kind.ready;\n"
                            "    pointer->inner.value = 42;\n"
                            "    return pointer->inner.value;\n"
                            "}\n"};

    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find("rep stosb"), std::string::npos);
    EXPECT_NE(assembly.find("leaq 0(%rax), %rax"), std::string::npos);
    EXPECT_NE(assembly.find("leaq 4(%rax), %rax"), std::string::npos);
    EXPECT_NE(assembly.find("movb %cl, (%rax)"), std::string::npos);
    EXPECT_NE(assembly.find("movl %ecx, (%rax)"), std::string::npos);
    EXPECT_NE(assembly.find("testq %rax, %rax"), std::string::npos);
}

TEST(X8664BackendTest, EmitsOverlapSafeInlineMemoryCopies) {
    FrontendHarness harness{
        "module test.memory_copy_codegen;\n"
        "struct Pair { left: i32; right: i32; }\n"
        "fn main() -> i32 {\n"
        "    var first: Pair = { left = 20, right = 22, };\n"
        "    var second: Pair = first;\n"
        "    second = first;\n"
        "    return second.left + second.right;\n"
        "}\n"};

    ASSERT_TRUE(harness.EmitAssembly()) << harness.Diagnostics();
    const std::string assembly = harness.Assembly();
    EXPECT_NE(assembly.find("movq $8, %rcx"), std::string::npos);
    EXPECT_NE(assembly.find("cmpq %rsi, %rdi"), std::string::npos);
    EXPECT_NE(assembly.find("leaq 8(%rsi), %rax"), std::string::npos);
    EXPECT_NE(assembly.find("std\n    rep movsb\n    cld"), std::string::npos);
    EXPECT_NE(assembly.find("cld\n    rep movsb"), std::string::npos);
    EXPECT_EQ(assembly.find("call memmove"), std::string::npos);
}

}
