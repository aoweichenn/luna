#include "luna/frontend/type/type.h"

#include <gtest/gtest.h>

TEST(TypeTest, ReportsCanonicalProperties) {
    EXPECT_STREQ(luna_type_kind_name(LUNA_TYPE_I8), "i8");
    EXPECT_TRUE(luna_type_kind_is_integer(LUNA_TYPE_I8));
    EXPECT_TRUE(luna_type_kind_is_signed_integer(LUNA_TYPE_I8));
    EXPECT_FALSE(luna_type_kind_is_unsigned_integer(LUNA_TYPE_I8));
    EXPECT_EQ(luna_type_kind_bit_width(LUNA_TYPE_I8), 8U);
    EXPECT_STREQ(luna_type_kind_name(LUNA_TYPE_I16), "i16");
    EXPECT_TRUE(luna_type_kind_is_integer(LUNA_TYPE_I16));
    EXPECT_TRUE(luna_type_kind_is_signed_integer(LUNA_TYPE_I16));
    EXPECT_FALSE(luna_type_kind_is_unsigned_integer(LUNA_TYPE_I16));
    EXPECT_EQ(luna_type_kind_bit_width(LUNA_TYPE_I16), 16U);
    EXPECT_STREQ(luna_type_kind_name(LUNA_TYPE_I32), "i32");
    EXPECT_TRUE(luna_type_kind_is_integer(LUNA_TYPE_I32));
    EXPECT_TRUE(luna_type_kind_is_signed_integer(LUNA_TYPE_I32));
    EXPECT_FALSE(luna_type_kind_is_unsigned_integer(LUNA_TYPE_I32));
    EXPECT_EQ(luna_type_kind_bit_width(LUNA_TYPE_I32), 32U);
    EXPECT_STREQ(luna_type_kind_name(LUNA_TYPE_I64), "i64");
    EXPECT_TRUE(luna_type_kind_is_integer(LUNA_TYPE_I64));
    EXPECT_TRUE(luna_type_kind_is_signed_integer(LUNA_TYPE_I64));
    EXPECT_EQ(luna_type_kind_bit_width(LUNA_TYPE_I64), 64U);
    EXPECT_STREQ(luna_type_kind_name(LUNA_TYPE_U8), "u8");
    EXPECT_TRUE(luna_type_kind_is_integer(LUNA_TYPE_U8));
    EXPECT_TRUE(luna_type_kind_is_unsigned_integer(LUNA_TYPE_U8));
    EXPECT_FALSE(luna_type_kind_is_signed_integer(LUNA_TYPE_U8));
    EXPECT_EQ(luna_type_kind_bit_width(LUNA_TYPE_U8), 8U);
    EXPECT_STREQ(luna_type_kind_name(LUNA_TYPE_U16), "u16");
    EXPECT_TRUE(luna_type_kind_is_integer(LUNA_TYPE_U16));
    EXPECT_TRUE(luna_type_kind_is_unsigned_integer(LUNA_TYPE_U16));
    EXPECT_FALSE(luna_type_kind_is_signed_integer(LUNA_TYPE_U16));
    EXPECT_EQ(luna_type_kind_bit_width(LUNA_TYPE_U16), 16U);
    EXPECT_STREQ(luna_type_kind_name(LUNA_TYPE_U32), "u32");
    EXPECT_TRUE(luna_type_kind_is_integer(LUNA_TYPE_U32));
    EXPECT_TRUE(luna_type_kind_is_unsigned_integer(LUNA_TYPE_U32));
    EXPECT_FALSE(luna_type_kind_is_signed_integer(LUNA_TYPE_U32));
    EXPECT_EQ(luna_type_kind_bit_width(LUNA_TYPE_U32), 32U);
    EXPECT_STREQ(luna_type_kind_name(LUNA_TYPE_U64), "u64");
    EXPECT_TRUE(luna_type_kind_is_integer(LUNA_TYPE_U64));
    EXPECT_TRUE(luna_type_kind_is_unsigned_integer(LUNA_TYPE_U64));
    EXPECT_EQ(luna_type_kind_bit_width(LUNA_TYPE_U64), 64U);
    EXPECT_FALSE(luna_type_kind_is_integer(LUNA_TYPE_BOOL));
    EXPECT_FALSE(luna_type_kind_is_signed_integer(LUNA_TYPE_BOOL));
    EXPECT_FALSE(luna_type_kind_is_unsigned_integer(LUNA_TYPE_BOOL));
    EXPECT_EQ(luna_type_kind_bit_width(LUNA_TYPE_BOOL), 1U);
    EXPECT_EQ(luna_type_kind_bit_width(LUNA_TYPE_VOID), 0U);
}
