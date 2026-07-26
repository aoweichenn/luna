#include "luna/frontend/type/type.h"

#include <gtest/gtest.h>

TEST(TypeTest, ReportsCanonicalProperties) {
    EXPECT_STREQ(luna_type_kind_name(LUNA_TYPE_I32), "i32");
    EXPECT_TRUE(luna_type_kind_is_integer(LUNA_TYPE_I32));
    EXPECT_EQ(luna_type_kind_bit_width(LUNA_TYPE_I32), 32U);
    EXPECT_FALSE(luna_type_kind_is_integer(LUNA_TYPE_BOOL));
    EXPECT_EQ(luna_type_kind_bit_width(LUNA_TYPE_BOOL), 1U);
    EXPECT_EQ(luna_type_kind_bit_width(LUNA_TYPE_VOID), 0U);
}
