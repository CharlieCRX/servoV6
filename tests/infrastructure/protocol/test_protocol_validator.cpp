// tests/infrastructure/protocol/test_protocol_validator.cpp
// P0: ProtocolConstraintValidator TDD 测试
#include <gtest/gtest.h>
#include "infrastructure/plc/protocol/validator/ProtocolConstraintValidator.h"
#include "infrastructure/plc/protocol/validator/ProtocolViolation.h"
#include "infrastructure/plc/protocol/RegisterRegistry.h"
#include "infrastructure/plc/protocol/RegisterMetadata.h"
#include <vector>

using namespace plc::protocol;

// ============================================================
// 辅助工厂函数：快速构造 RegisterInfo
// ============================================================

static RegisterInfo makeReg(
    RegisterArea area,
    uint16_t address,
    RegisterType type,
    RegisterAccess access = RegisterAccess::ReadWrite,
    RegisterBehavior behavior = RegisterBehavior::Level,
    RegisterGroup group = RegisterGroup::Command,
    const char* desc = "test",
    uint32_t pulseWidthMs = 0)
{
    return RegisterInfo{
        area,
        address,
        type,
        access,
        behavior,
        group,
        "",       // unit
        desc,     // description
        pulseWidthMs,
        std::nullopt // endianOverride
    };
}

// ============================================================
// Test Suite: ProtocolConstraintValidator
// ============================================================

class ProtocolConstraintValidatorTest : public ::testing::Test {
protected:
    ProtocolConstraintValidator validator;
};

// ----------------------------------------------------------
// Test 1: 空 Registry → 无违规
// ----------------------------------------------------------
TEST_F(ProtocolConstraintValidatorTest, validate_empty_registry_returns_no_violations) {
    RegisterRegistry registry;
    auto violations = validator.validate(registry);
    EXPECT_TRUE(violations.empty());
}

// ----------------------------------------------------------
// Test 2: 同一 Area 内地址重叠 (Float32 占 124-125, Int16 在 125)
// ----------------------------------------------------------
TEST_F(ProtocolConstraintValidatorTest, detect_address_overlap_same_area_float32_int16) {
    RegisterRegistry registry;
    registry.addAll({
        makeReg(RegisterArea::HoldingReg, 124, RegisterType::Float32,
                RegisterAccess::ReadOnly, RegisterBehavior::Continuous,
                RegisterGroup::Feedback, "ABS_POSITION"),
        makeReg(RegisterArea::HoldingReg, 125, RegisterType::Int16,
                RegisterAccess::ReadOnly, RegisterBehavior::Continuous,
                RegisterGroup::Feedback, "SOME_VALUE"),
    });

    auto violations = validator.validate(registry);
    ASSERT_EQ(violations.size(), 1);
    EXPECT_EQ(violations[0].ruleId, "R01");
    EXPECT_EQ(violations[0].severity, ProtocolViolation::Severity::Error);
}

// ----------------------------------------------------------
// Test 3: 不同 Area 相同地址 → 无冲突
// ----------------------------------------------------------
TEST_F(ProtocolConstraintValidatorTest, no_overlap_different_areas) {
    RegisterRegistry registry;
    registry.addAll({
        makeReg(RegisterArea::Coil, 101, RegisterType::Bool,
                RegisterAccess::ReadOnly, RegisterBehavior::Continuous,
                RegisterGroup::Feedback, "MOVE_DONE"),
        makeReg(RegisterArea::HoldingReg, 101, RegisterType::Int16,
                RegisterAccess::ReadOnly, RegisterBehavior::Continuous,
                RegisterGroup::Feedback, "STATE"),
    });

    auto violations = validator.validate(registry);
    EXPECT_TRUE(violations.empty());
}

// ----------------------------------------------------------
// Test 4: Coil 区域使用非 Bool 类型 → R02 Error
// ----------------------------------------------------------
TEST_F(ProtocolConstraintValidatorTest, detect_coil_with_non_bool_type) {
    RegisterRegistry registry;
    registry.addAll({
        makeReg(RegisterArea::Coil, 1, RegisterType::Float32,
                RegisterAccess::ReadWrite, RegisterBehavior::Level,
                RegisterGroup::Command, "BAD_COIL"),
    });

    auto violations = validator.validate(registry);
    ASSERT_EQ(violations.size(), 1);
    EXPECT_EQ(violations[0].ruleId, "R02");
    EXPECT_EQ(violations[0].severity, ProtocolViolation::Severity::Error);
}

// ----------------------------------------------------------
// Test 5: Coil 区域使用 Bool 类型 → 合法
// ----------------------------------------------------------
TEST_F(ProtocolConstraintValidatorTest, coil_with_bool_is_valid) {
    RegisterRegistry registry;
    registry.addAll({
        makeReg(RegisterArea::Coil, 1, RegisterType::Bool,
                RegisterAccess::ReadWrite, RegisterBehavior::Level,
                RegisterGroup::Command, "ENABLE"),
    });

    auto violations = validator.validate(registry);
    EXPECT_TRUE(violations.empty());
}

// ----------------------------------------------------------
// Test 6: Feedback 组非 ReadOnly → R03 Error
// ----------------------------------------------------------
TEST_F(ProtocolConstraintValidatorTest, detect_feedback_not_readonly) {
    RegisterRegistry registry;
    registry.addAll({
        makeReg(RegisterArea::HoldingReg, 100, RegisterType::Int16,
                RegisterAccess::ReadWrite,  // ← 应该是 ReadOnly
                RegisterBehavior::Continuous,
                RegisterGroup::Feedback, "CUR_POSITION"),
    });

    auto violations = validator.validate(registry);
    ASSERT_EQ(violations.size(), 1);
    EXPECT_EQ(violations[0].ruleId, "R03");
    EXPECT_EQ(violations[0].severity, ProtocolViolation::Severity::Error);
}

// ----------------------------------------------------------
// Test 7: Feedback 组 ReadOnly → 合法
// ----------------------------------------------------------
TEST_F(ProtocolConstraintValidatorTest, feedback_readonly_is_valid) {
    RegisterRegistry registry;
    registry.addAll({
        makeReg(RegisterArea::HoldingReg, 100, RegisterType::Int16,
                RegisterAccess::ReadOnly,
                RegisterBehavior::Continuous,
                RegisterGroup::Feedback, "CUR_POSITION"),
    });

    auto violations = validator.validate(registry);
    EXPECT_TRUE(violations.empty());
}

// ----------------------------------------------------------
// Test 8: ManualResetEdgeTrigger 且 pulseWidthMs=0 → R04 Error
// ----------------------------------------------------------
TEST_F(ProtocolConstraintValidatorTest, detect_edge_trigger_without_pulse_width) {
    RegisterRegistry registry;
    registry.addAll({
        makeReg(RegisterArea::Coil, 10, RegisterType::Bool,
                RegisterAccess::ReadWrite,
                RegisterBehavior::ManualResetEdgeTrigger,  // ← 需要脉冲宽度
                RegisterGroup::Command, "SET_ZERO",
                0),  // pulseWidthMs = 0 → 违规
    });

    auto violations = validator.validate(registry);
    ASSERT_EQ(violations.size(), 1);
    EXPECT_EQ(violations[0].ruleId, "R04");
    EXPECT_EQ(violations[0].severity, ProtocolViolation::Severity::Error);
}

// ----------------------------------------------------------
// Test 9: ManualResetEdgeTrigger 且 pulseWidthMs>0 → 合法
// ----------------------------------------------------------
TEST_F(ProtocolConstraintValidatorTest, edge_trigger_with_pulse_width_is_valid) {
    RegisterRegistry registry;
    registry.addAll({
        makeReg(RegisterArea::Coil, 10, RegisterType::Bool,
                RegisterAccess::ReadWrite,
                RegisterBehavior::ManualResetEdgeTrigger,
                RegisterGroup::Command, "SET_ZERO",
                50),  // pulseWidthMs = 50 → 合法
    });

    auto violations = validator.validate(registry);
    EXPECT_TRUE(violations.empty());
}

// ----------------------------------------------------------
// Test 10: 同时触发多条规则 → 全部报告
// ----------------------------------------------------------
TEST_F(ProtocolConstraintValidatorTest, multiple_violations_reported_together) {
    RegisterRegistry registry;
    registry.addAll({
        // 触发 R02: Coil + 非 Bool
        makeReg(RegisterArea::Coil, 1, RegisterType::Float32,
                RegisterAccess::ReadWrite, RegisterBehavior::Level,
                RegisterGroup::Command, "BAD_COIL_TYPE"),
        // 触发 R03: Feedback + 非 ReadOnly
        makeReg(RegisterArea::HoldingReg, 200, RegisterType::Int16,
                RegisterAccess::ReadWrite,
                RegisterBehavior::Continuous,
                RegisterGroup::Feedback, "BAD_FB_ACCESS"),
        // 触发 R04: EdgeTrigger + pulseWidthMs=0
        makeReg(RegisterArea::Coil, 10, RegisterType::Bool,
                RegisterAccess::ReadWrite,
                RegisterBehavior::ManualResetEdgeTrigger,
                RegisterGroup::Command, "BAD_EDGE_NO_PULSE",
                0),
    });

    auto violations = validator.validate(registry);
    ASSERT_EQ(violations.size(), 3);

    // 验证所有三条规则都被触发
    bool hasR02 = false, hasR03 = false, hasR04 = false;
    for (const auto& v : violations) {
        if (v.ruleId == "R02") hasR02 = true;
        if (v.ruleId == "R03") hasR03 = true;
        if (v.ruleId == "R04") hasR04 = true;
    }
    EXPECT_TRUE(hasR02);
    EXPECT_TRUE(hasR03);
    EXPECT_TRUE(hasR04);
}

// ----------------------------------------------------------
// Test 11: 同一 Area 内不重叠地址 → 无违规
// ----------------------------------------------------------
TEST_F(ProtocolConstraintValidatorTest, no_overlap_same_area_non_overlapping) {
    RegisterRegistry registry;
    registry.addAll({
        makeReg(RegisterArea::HoldingReg, 100, RegisterType::Int16,
                RegisterAccess::ReadOnly, RegisterBehavior::Continuous,
                RegisterGroup::Feedback, "REG_100"),
        makeReg(RegisterArea::HoldingReg, 102, RegisterType::Int16,
                RegisterAccess::ReadOnly, RegisterBehavior::Continuous,
                RegisterGroup::Feedback, "REG_102"),
    });

    auto violations = validator.validate(registry);
    EXPECT_TRUE(violations.empty());
}

// ----------------------------------------------------------
// Test 12: Float32 跨两个寄存器与第二个寄存器冲突
//          HoldingReg 124 Float32 (占 124-125) + HoldingReg 125 Int16
// ----------------------------------------------------------
TEST_F(ProtocolConstraintValidatorTest, detect_overlap_float32_spanning_two_registers) {
    RegisterRegistry registry;
    registry.addAll({
        makeReg(RegisterArea::HoldingReg, 124, RegisterType::Float32,
                RegisterAccess::ReadOnly, RegisterBehavior::Continuous,
                RegisterGroup::Feedback, "TARGET_POS"),
        makeReg(RegisterArea::HoldingReg, 125, RegisterType::Int16,
                RegisterAccess::ReadWrite, RegisterBehavior::Level,
                RegisterGroup::Command, "OVERRIDE_VALUE"),
    });

    auto violations = validator.validate(registry);
    ASSERT_EQ(violations.size(), 1);
    EXPECT_EQ(violations[0].ruleId, "R01");
    EXPECT_EQ(violations[0].severity, ProtocolViolation::Severity::Error);
}
