/**
 * @file test_gantry_sync.cpp
 * @brief 龙门同步相关值对象单元测试
 *
 * 覆盖设计文档约束：
 *   约束8  - 统一位置定义
 *   约束9  - 位置一致性（镜像关系）
 *   约束10 - 逻辑位置计算
 *   约束11 - 位置一致性失效
 *   约束13 - 联动建立条件
 *   约束14 - 联动期间持续约束
 *   约束15 - 限位优先级最高
 *   约束16 - 限位后行为限制
 *   约束17 - 报警约束
 *
 * 测试值对象：
 *   - PositionConsistency
 *   - CouplingCondition
 *   - SafetyCheckResult
 */

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "value/PositionConsistency.h"
#include "value/CouplingCondition.h"
#include "value/SafetyCheckResult.h"
#include "value/MotionDirection.h"

// ═══════════════════════════════════════════════════════════
// PositionConsistency 测试
// ═══════════════════════════════════════════════════════════

// --- 偏差计算 ---

TEST(PositionConsistencyTest, PerfectMirror_HasZeroDeviation) {
    // 约束9: X1.pos ≈ -X2.pos（镜像关系）
    // 完美镜像状态：X1=100.0, X2=-100.0
    double dev = PositionConsistency::computeDeviation(100.0, -100.0);
    EXPECT_DOUBLE_EQ(dev, 0.0);
}

TEST(PositionConsistencyTest, BothAtZero_HasZeroDeviation) {
    // 双轴都在零点（初始状态或回零后）
    double dev = PositionConsistency::computeDeviation(0.0, 0.0);
    EXPECT_DOUBLE_EQ(dev, 0.0);
}

TEST(PositionConsistencyTest, SmallMisalignment_ReturnsPositiveDeviation) {
    // X1=50.0, X2=-49.99 → 偏差 ≈ 0.01（容忍浮点误差）
    double dev = PositionConsistency::computeDeviation(50.0, -49.99);
    EXPECT_NEAR(dev, 0.01, 1e-12);
}

TEST(PositionConsistencyTest, LargeMisalignment_ReturnsCorrectDeviation) {
    // X1=100.0, X2=-95.0 → 偏差 5.0
    double dev = PositionConsistency::computeDeviation(100.0, -95.0);
    EXPECT_DOUBLE_EQ(dev, 5.0);
}

TEST(PositionConsistencyTest, BothPositive_NotMirror_ReturnsLargeDeviation) {
    // 双轴同向（非镜像状态）- 最极端的失同步
    double dev = PositionConsistency::computeDeviation(100.0, 100.0);
    EXPECT_DOUBLE_EQ(dev, 200.0);
}

TEST(PositionConsistencyTest, BothNegative_ReturnsAbsoluteDeviation) {
    double dev = PositionConsistency::computeDeviation(-100.0, -100.0);
    EXPECT_DOUBLE_EQ(dev, 200.0);  // |-100 + -100| = |-200| = 200
}

TEST(PositionConsistencyTest, DeviationIsAlwaysNonNegative) {
    // 各种输入下偏差始终 >= 0
    EXPECT_GE(PositionConsistency::computeDeviation(50.0, -30.0), 0.0);
    EXPECT_GE(PositionConsistency::computeDeviation(-50.0, 30.0), 0.0);
    EXPECT_GE(PositionConsistency::computeDeviation(0.0, 0.0), 0.0);
    EXPECT_GE(PositionConsistency::computeDeviation(-10.0, -20.0), 0.0);
}

// --- 一致性判断（约束11）---

TEST(PositionConsistencyTest, WithinEpsilon_IsConsistent) {
    // 偏差 = 0.005 <= epsilon 0.01 → 一致
    EXPECT_TRUE(PositionConsistency::isConsistent(50.0, -49.995, 0.01));
}

TEST(PositionConsistencyTest, ExactlyAtEpsilon_IsConsistent) {
    // 偏差 = 0.01 == epsilon 0.01 → 一致（<=）
    EXPECT_TRUE(PositionConsistency::isConsistent(50.0, -49.99, 0.01));
}

TEST(PositionConsistencyTest, ExceedsEpsilon_IsNotConsistent) {
    // 约束11: |X1.pos + X2.pos| > epsilon → 不同步
    EXPECT_FALSE(PositionConsistency::isConsistent(50.0, -49.97, 0.01));
}

TEST(PositionConsistencyTest, ZeroDeviation_AlwaysConsistent) {
    // 完美镜像在任何 epsilon 下都应该通过
    EXPECT_TRUE(PositionConsistency::isConsistent(0.0, 0.0, 0.0));
    EXPECT_TRUE(PositionConsistency::isConsistent(100.0, -100.0, 0.0));
    EXPECT_TRUE(PositionConsistency::isConsistent(100.0, -100.0, 1e-9));
}

TEST(PositionConsistencyTest, UsesDefaultEpsilon) {
    // 不传 epsilon 参使用 kDefaultEpsilon = 0.01
    EXPECT_TRUE(PositionConsistency::isConsistent(50.0, -49.999));
    EXPECT_FALSE(PositionConsistency::isConsistent(50.0, -49.0));
}

// --- 逻辑位置计算（约束10）---

TEST(PositionConsistencyTest, LogicalPosition_EqualsX1Position) {
    // 约束10: X.position = X1.pos
    EXPECT_DOUBLE_EQ(PositionConsistency::computeLogicalPosition(100.0), 100.0);
    EXPECT_DOUBLE_EQ(PositionConsistency::computeLogicalPosition(-50.0), -50.0);
    EXPECT_DOUBLE_EQ(PositionConsistency::computeLogicalPosition(0.0), 0.0);
}

// --- 镜像推导 ---

TEST(PositionConsistencyTest, ExpectedX1FromX2_IsNegated) {
    EXPECT_DOUBLE_EQ(PositionConsistency::expectedX1FromX2(50.0), -50.0);
    EXPECT_DOUBLE_EQ(PositionConsistency::expectedX1FromX2(-100.0), 100.0);
    EXPECT_DOUBLE_EQ(PositionConsistency::expectedX1FromX2(0.0), 0.0);
}

// --- 诊断描述 ---

TEST(PositionConsistencyTest, DescribeDeviation_ContainsDeviationValue) {
    std::string desc = PositionConsistency::describeDeviation(50.0, -49.9, 0.01);
    EXPECT_NE(desc.find("0.1"), std::string::npos);  // 偏差 = 0.1
    EXPECT_NE(desc.find("0.01"), std::string::npos);  // epsilon = 0.01
}

TEST(PositionConsistencyTest, DescribeDeviation_ForConsistentCase) {
    std::string desc = PositionConsistency::describeDeviation(50.0, -49.995, 0.01);
    EXPECT_NE(desc.find("0.005"), std::string::npos);
}

// --- 边界值 ---

TEST(PositionConsistencyTest, VeryLargeValues) {
    double dev = PositionConsistency::computeDeviation(1e9, -1e9);
    EXPECT_DOUBLE_EQ(dev, 0.0);
}

TEST(PositionConsistencyTest, NaN_ProducesNaNDeviation) {
    double dev = PositionConsistency::computeDeviation(
        std::numeric_limits<double>::quiet_NaN(), 100.0);
    EXPECT_TRUE(std::isnan(dev));
}

TEST(PositionConsistencyTest, NaN_IsNotConsistent) {
    // NaN 偏差 <= epsilon 永远为 false
    EXPECT_FALSE(PositionConsistency::isConsistent(
        std::numeric_limits<double>::quiet_NaN(), 100.0, 0.01));
}


// ═══════════════════════════════════════════════════════════
// CouplingCondition 测试（约束13/14）
// ═══════════════════════════════════════════════════════════

// --- 全部满足 ---

TEST(CouplingConditionTest, AllConditionsMet_Allowed) {
    // 约束13 全部满足
    auto result = CouplingCondition::checkAll(
        true, true,   // X1/X2 使能
        false, false, // 无报警、无限位
        100.0, -100.0 // 位置一致
    );
    EXPECT_TRUE(result.allowed);
    EXPECT_TRUE(result.failReason.empty());
    EXPECT_TRUE(result);  // 隐式 bool 转换
}

// --- 使能条件 ---

TEST(CouplingConditionTest, X1NotEnabled_Rejected) {
    auto result = CouplingCondition::checkAll(
        false, true, false, false, 100.0, -100.0);
    EXPECT_FALSE(result.allowed);
    EXPECT_FALSE(result);
    EXPECT_NE(result.failReason.find("X1"), std::string::npos);
}

TEST(CouplingConditionTest, X2NotEnabled_Rejected) {
    auto result = CouplingCondition::checkAll(
        true, false, false, false, 100.0, -100.0);
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("X2"), std::string::npos);
}

TEST(CouplingConditionTest, BothNotEnabled_Rejected_CheckOrder) {
    // 验证检查顺序：X1 在前，所以先报 X1 未使能
    auto result = CouplingCondition::checkAll(
        false, false, false, false, 100.0, -100.0);
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("X1"), std::string::npos);
}

// --- 报警条件 ---

TEST(CouplingConditionTest, AlarmActive_Rejected) {
    auto result = CouplingCondition::checkAll(
        true, true, true, false, 100.0, -100.0);
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("Alarm"), std::string::npos);
}

// --- 限位条件 ---

TEST(CouplingConditionTest, LimitTriggered_Rejected) {
    auto result = CouplingCondition::checkAll(
        true, true, false, true, 100.0, -100.0);
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("Limit"), std::string::npos);
}

// --- 位置一致性条件 ---

TEST(CouplingConditionTest, PositionNotConsistent_Rejected) {
    auto result = CouplingCondition::checkAll(
        true, true, false, false, 100.0, -90.0);  // 偏差 10mm
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("deviation"), std::string::npos);
}

// --- 多重不满足：优先报第一个 ---

TEST(CouplingConditionTest, MultipleFailures_ReportsFirst) {
    // X1 未使能 + 报警 → 应该先报 X1 未使能
    auto result = CouplingCondition::checkAll(
        false, true, true, false, 100.0, -100.0);
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("X1"), std::string::npos);
    EXPECT_EQ(result.failReason.find("Alarm"), std::string::npos);
}

// --- 联动维持检查（约束14）---

TEST(CouplingConditionTest, PositionOnlyCheck_PositionConsistent_Allowed) {
    // 约束14: 联动运行期间持续满足位置一致性
    auto result = CouplingCondition::checkPositionOnly(100.0, -100.0);
    EXPECT_TRUE(result.allowed);
    EXPECT_TRUE(result.failReason.empty());
}

TEST(CouplingConditionTest, PositionOnlyCheck_PositionDeviated_Rejected) {
    auto result = CouplingCondition::checkPositionOnly(100.0, -95.0, 0.1);
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("deviation"), std::string::npos);
}

TEST(CouplingConditionTest, PositionOnlyCheck_CustomEpsilon) {
    // 使用更宽松的 epsilon
    auto result = CouplingCondition::checkPositionOnly(100.0, -50.0, 60.0);
    EXPECT_TRUE(result.allowed);  // 偏差 50 <= 60
}

// --- Result 结构体隐式 bool 转换 ---

TEST(CouplingConditionTest, Result_ImplicitBoolConversion) {
    auto allowed = CouplingCondition::checkAll(
        true, true, false, false, 100.0, -100.0);
    auto rejected = CouplingCondition::checkAll(
        false, true, false, false, 100.0, -100.0);

    // 可在 if 语句中直接使用
    if (allowed) {
        SUCCEED();
    } else {
        FAIL() << "Expected allowed to be true";
    }

    if (rejected) {
        FAIL() << "Expected rejected to be false";
    } else {
        SUCCEED();
    }
}


// ═══════════════════════════════════════════════════════════
// SafetyCheckResult 测试（约束15/16/17）
// ═══════════════════════════════════════════════════════════

// --- 工厂方法 ---

TEST(SafetyCheckResultTest, Factory_Allowed) {
    auto result = SafetyCheckResult::allowed();
    EXPECT_TRUE(result.isAllowed());
    EXPECT_FALSE(result.isRejected());
    EXPECT_EQ(result.verdict(), SafetyCheckResult::Verdict::Allowed);
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_NE(result.reason().find("Allowed"), std::string::npos);
}

TEST(SafetyCheckResultTest, Factory_RejectedDueToAlarm) {
    auto result = SafetyCheckResult::rejectedDueToAlarm();
    EXPECT_FALSE(result.isAllowed());
    EXPECT_TRUE(result.isRejected());
    EXPECT_EQ(result.verdict(), SafetyCheckResult::Verdict::Rejected_Alarm);
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_NE(result.reason().find("alarm"), std::string::npos);
}

TEST(SafetyCheckResultTest, Factory_RejectedDueToForwardLimit) {
    auto result = SafetyCheckResult::rejectedDueToForwardLimit();
    EXPECT_EQ(result.verdict(), SafetyCheckResult::Verdict::Rejected_LimitForward);
    EXPECT_NE(result.reason().find("forward"), std::string::npos);
}

TEST(SafetyCheckResultTest, Factory_RejectedDueToBackwardLimit) {
    auto result = SafetyCheckResult::rejectedDueToBackwardLimit();
    EXPECT_EQ(result.verdict(), SafetyCheckResult::Verdict::Rejected_LimitBackward);
    EXPECT_NE(result.reason().find("backward"), std::string::npos);
}

TEST(SafetyCheckResultTest, Factory_RejectedDueToLimit_Generic) {
    auto result = SafetyCheckResult::rejectedDueToLimit();
    EXPECT_EQ(result.verdict(), SafetyCheckResult::Verdict::Rejected_Limit);
}

// --- 值相等 ---

TEST(SafetyCheckResultTest, Equality_SameVerdictAndReason_AreEqual) {
    auto a = SafetyCheckResult::allowed();
    auto b = SafetyCheckResult::allowed();
    EXPECT_EQ(a, b);
}

TEST(SafetyCheckResultTest, Equality_DifferentVerdict_NotEqual) {
    auto a = SafetyCheckResult::allowed();
    auto b = SafetyCheckResult::rejectedDueToAlarm();
    EXPECT_NE(a, b);
}

TEST(SafetyCheckResultTest, Equality_DifferentReason_NotEqual) {
    auto a = SafetyCheckResult(SafetyCheckResult::Verdict::Allowed, "reason A");
    auto b = SafetyCheckResult(SafetyCheckResult::Verdict::Allowed, "reason B");
    EXPECT_NE(a, b);
}

// --- checkMotionSafety: 正常状态 ---

TEST(CheckMotionSafetyTest, NormalState_AllDirectionsAllowed) {
    // 无报警、无限位 → 所有方向都允许
    EXPECT_TRUE(checkMotionSafety(false, false, false, MotionDirection::Forward));
    EXPECT_TRUE(checkMotionSafety(false, false, false, MotionDirection::Backward));
}

// --- checkMotionSafety: 报警约束（约束17）---

TEST(CheckMotionSafetyTest, Alarm_AllDirectionsBlocked) {
    // 约束17: 报警状态下禁止所有运动
    EXPECT_FALSE(checkMotionSafety(true, false, false, MotionDirection::Forward));
    EXPECT_FALSE(checkMotionSafety(true, false, false, MotionDirection::Backward));
}

TEST(CheckMotionSafetyTest, Alarm_OverridesLimit) {
    // 约束17: 报警优先级最高，即使有限位也要先处理报警
    auto r1 = checkMotionSafety(true, true, false, MotionDirection::Backward);
    EXPECT_EQ(r1.verdict(), SafetyCheckResult::Verdict::Rejected_Alarm);

    auto r2 = checkMotionSafety(true, false, true, MotionDirection::Forward);
    EXPECT_EQ(r2.verdict(), SafetyCheckResult::Verdict::Rejected_Alarm);
}

// --- checkMotionSafety: 正向限位（约束16）---

TEST(CheckMotionSafetyTest, ForwardLimit_BlocksForwardDirection) {
    // 约束16: 触发正向限位 → 禁止正向运动
    auto result = checkMotionSafety(false, true, false, MotionDirection::Forward);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.verdict(), SafetyCheckResult::Verdict::Rejected_LimitForward);
}

TEST(CheckMotionSafetyTest, ForwardLimit_AllowsBackwardDirection) {
    // 约束16: 触发正向限位 → 允许远离限位（Backward）的 Jog
    auto result = checkMotionSafety(false, true, false, MotionDirection::Backward);
    EXPECT_TRUE(result);
    EXPECT_EQ(result.verdict(), SafetyCheckResult::Verdict::Allowed);
}

// --- checkMotionSafety: 负向限位（约束16）---

TEST(CheckMotionSafetyTest, BackwardLimit_BlocksBackwardDirection) {
    auto result = checkMotionSafety(false, false, true, MotionDirection::Backward);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.verdict(), SafetyCheckResult::Verdict::Rejected_LimitBackward);
}

TEST(CheckMotionSafetyTest, BackwardLimit_AllowsForwardDirection) {
    auto result = checkMotionSafety(false, false, true, MotionDirection::Forward);
    EXPECT_TRUE(result);
    EXPECT_EQ(result.verdict(), SafetyCheckResult::Verdict::Allowed);
}

// --- checkMotionSafety: 双向限位同时触发 ---

TEST(CheckMotionSafetyTest, BothLimitsActive_BlocksForwardFirst) {
    // 同时触发双向限位（极端故障场景）
    // 按检查顺序：先检查正向限位
    auto result = checkMotionSafety(false, true, true, MotionDirection::Forward);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.verdict(), SafetyCheckResult::Verdict::Rejected_LimitForward);
}

TEST(CheckMotionSafetyTest, BothLimitsActive_BlocksBackwardToo) {
    // 双向限位同时激活 → 两个方向都被拒绝
    auto result = checkMotionSafety(false, true, true, MotionDirection::Backward);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.verdict(), SafetyCheckResult::Verdict::Rejected_LimitBackward);
}

// --- checkMotionSafety: 方向安全矩阵 ---

struct MotionSafetyParam {
    bool alarm;
    bool fwdLimit;
    bool bwdLimit;
    MotionDirection dir;
    SafetyCheckResult::Verdict expected;
};

class MotionSafetyMatrixTest : public ::testing::TestWithParam<MotionSafetyParam> {};

TEST_P(MotionSafetyMatrixTest, FullMatrix) {
    auto& p = GetParam();
    auto result = checkMotionSafety(p.alarm, p.fwdLimit, p.bwdLimit, p.dir);
    EXPECT_EQ(result.verdict(), p.expected)
        << "alarm=" << p.alarm
        << " fwdLimit=" << p.fwdLimit
        << " bwdLimit=" << p.bwdLimit
        << " dir=" << (p.dir == MotionDirection::Forward ? "Forward" : "Backward");
}

INSTANTIATE_TEST_SUITE_P(
    MotionSafety,
    MotionSafetyMatrixTest,
    ::testing::Values(
        //                          alarm fwdLim bwdLim direction         expected
        MotionSafetyParam{false, false, false, MotionDirection::Forward,  SafetyCheckResult::Verdict::Allowed},
        MotionSafetyParam{false, false, false, MotionDirection::Backward, SafetyCheckResult::Verdict::Allowed},
        MotionSafetyParam{true,  false, false, MotionDirection::Forward,  SafetyCheckResult::Verdict::Rejected_Alarm},
        MotionSafetyParam{true,  false, false, MotionDirection::Backward, SafetyCheckResult::Verdict::Rejected_Alarm},
        MotionSafetyParam{false, true,  false, MotionDirection::Forward,  SafetyCheckResult::Verdict::Rejected_LimitForward},
        MotionSafetyParam{false, true,  false, MotionDirection::Backward, SafetyCheckResult::Verdict::Allowed},
        MotionSafetyParam{false, false, true,  MotionDirection::Forward,  SafetyCheckResult::Verdict::Allowed},
        MotionSafetyParam{false, false, true,  MotionDirection::Backward, SafetyCheckResult::Verdict::Rejected_LimitBackward},
        MotionSafetyParam{false, true,  true,  MotionDirection::Forward,  SafetyCheckResult::Verdict::Rejected_LimitForward},
        MotionSafetyParam{false, true,  true,  MotionDirection::Backward, SafetyCheckResult::Verdict::Rejected_LimitBackward},
        MotionSafetyParam{true,  true,  false, MotionDirection::Backward, SafetyCheckResult::Verdict::Rejected_Alarm},
        MotionSafetyParam{true,  false, true,  MotionDirection::Forward,  SafetyCheckResult::Verdict::Rejected_Alarm}
    )
);
