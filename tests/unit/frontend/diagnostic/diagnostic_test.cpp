#include "test_support.hpp"

#include <gtest/gtest.h>

#include <string>

namespace luna::test {

TEST(DiagnosticTest, LimitsCaretUnderlineToTheDisplayedSourceLine) {
    FrontendHarness harness{"first\nsecond\n"};
    ASSERT_TRUE(harness.IsReady());
    const LunaSourceSpan span{
        .source = harness.Source(),
        .offset = 0U,
        .length = 12U,
        .line = 1U,
        .column = 1U,
    };

    luna_diagnostic_error(harness.DiagnosticEngine(), span, "test message");
    const std::string diagnostic = harness.Diagnostics();
    EXPECT_NE(diagnostic.find("  first\n  ^^^^^\n"), std::string::npos);
    EXPECT_EQ(diagnostic.find("^^^^^^^^^^^^"), std::string::npos);
}

TEST(DiagnosticTest, NotesPreserveTheErrorCountAndPointToSource) {
    FrontendHarness harness{"first\nsecond\n"};
    ASSERT_TRUE(harness.IsReady());
    const LunaSourceSpan span{
        .source = harness.Source(),
        .offset = 6U,
        .length = 6U,
        .line = 2U,
        .column = 1U,
    };

    luna_diagnostic_error(harness.DiagnosticEngine(), span, "primary");
    luna_diagnostic_note(harness.DiagnosticEngine(), span, "related");

    EXPECT_EQ(harness.ErrorCount(), 1U);
    const std::string diagnostic = harness.Diagnostics();
    EXPECT_NE(diagnostic.find("<test>:2:1: error: primary"), std::string::npos);
    EXPECT_NE(diagnostic.find("<test>:2:1: note: related"), std::string::npos);
}

}
