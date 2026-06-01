#include <gtest/gtest.h>
#include "infrastructure/plc/protocol/PlcValue.h"

using namespace plc::protocol;

// ============================================================================
// 第 1 步：基础构造 — 验证 variant 持有多态类型
// ============================================================================

TEST(PlcValueTest, ConstructBool_True_GetValueReturnsTrue) {
    PlcValue value = true;
    EXPECT_EQ(getValue<bool>(value), true);
}

TEST(PlcValueTest, ConstructBool_False_GetValueReturnsFalse) {
    PlcValue value = false;
    EXPECT_EQ(getValue<bool>(value), false);
}

TEST(PlcValueTest, ConstructFloat_Positive_GetValueReturnsCorrectValue) {
    PlcValue value = 150.5f;
    EXPECT_FLOAT_EQ(getValue<float>(value), 150.5f);
}

TEST(PlcValueTest, ConstructFloat_Negative_GetValueReturnsCorrectValue) {
    PlcValue value = -1.0f;
    EXPECT_FLOAT_EQ(getValue<float>(value), -1.0f);
}

TEST(PlcValueTest, ConstructInt16_Positive_GetValueReturnsCorrectValue) {
    PlcValue value = static_cast<int16_t>(1);
    EXPECT_EQ(getValue<int16_t>(value), 1);
}

TEST(PlcValueTest, ConstructInt16_Negative_GetValueReturnsCorrectValue) {
    PlcValue value = static_cast<int16_t>(-32768);
    EXPECT_EQ(getValue<int16_t>(value), -32768);
}

TEST(PlcValueTest, ConstructString_GetValueReturnsCorrectValue) {
    PlcValue value = std::string("hello");
    EXPECT_EQ(getValue<std::string>(value), "hello");
}


// ============================================================================
// 第 2 步：类型判别 — isBool / isInt16 / isFloat / isString
// ============================================================================

TEST(PlcValueTest, IsBool_TrueValue_ReturnsTrue) {
    PlcValue value = true;
    EXPECT_TRUE(isBool(value));
    EXPECT_FALSE(isInt16(value));
    EXPECT_FALSE(isFloat(value));
    EXPECT_FALSE(isString(value));
}

TEST(PlcValueTest, IsInt16_ReturnsTrue) {
    PlcValue value = static_cast<int16_t>(42);
    EXPECT_FALSE(isBool(value));
    EXPECT_TRUE(isInt16(value));
    EXPECT_FALSE(isFloat(value));
    EXPECT_FALSE(isString(value));
}

TEST(PlcValueTest, IsFloat_ReturnsTrue) {
    PlcValue value = 150.5f;
    EXPECT_FALSE(isBool(value));
    EXPECT_FALSE(isInt16(value));
    EXPECT_TRUE(isFloat(value));
    EXPECT_FALSE(isString(value));
}

TEST(PlcValueTest, IsString_ReturnsTrue) {
    PlcValue value = std::string("alarm_active");
    EXPECT_FALSE(isBool(value));
    EXPECT_FALSE(isInt16(value));
    EXPECT_FALSE(isFloat(value));
    EXPECT_TRUE(isString(value));
}


// ============================================================================
// 第 3 步：错误处理 — 类型不匹配应抛出 std::bad_variant_access
// ============================================================================

TEST(PlcValueTest, GetBool_FromFloat_ThrowsBadVariantAccess) {
    PlcValue value = 150.5f;
    EXPECT_THROW(getValue<bool>(value), std::bad_variant_access);
}

TEST(PlcValueTest, GetFloat_FromBool_ThrowsBadVariantAccess) {
    PlcValue value = true;
    EXPECT_THROW(getValue<float>(value), std::bad_variant_access);
}

TEST(PlcValueTest, GetInt16_FromFloat_ThrowsBadVariantAccess) {
    PlcValue value = 150.5f;
    EXPECT_THROW(getValue<int16_t>(value), std::bad_variant_access);
}

TEST(PlcValueTest, GetString_FromInt16_ThrowsBadVariantAccess) {
    PlcValue value = static_cast<int16_t>(1);
    EXPECT_THROW(getValue<std::string>(value), std::bad_variant_access);
}


// ============================================================================
// 第 4 步：边界值 — 验证 int16_t 的极值
// ============================================================================

TEST(PlcValueTest, ConstructInt16_MaxValue_ReturnsCorrect) {
    PlcValue value = static_cast<int16_t>(32767);
    EXPECT_EQ(getValue<int16_t>(value), 32767);
}

TEST(PlcValueTest, ConstructInt16_MinValue_ReturnsCorrect) {
    PlcValue value = static_cast<int16_t>(-32768);
    EXPECT_EQ(getValue<int16_t>(value), -32768);
}

TEST(PlcValueTest, ConstructFloat_Zero_ReturnsCorrect) {
    PlcValue value = 0.0f;
    EXPECT_FLOAT_EQ(getValue<float>(value), 0.0f);
}
