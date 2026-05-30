// =============================================================================
// TDD 阶段三：状态推导引擎单元测试
//
// 测试对象：plc::deriveAxisState() 纯函数
// 覆盖范围：15 项参数化组合 + 6 项边界场景
//
// 设计依据：
//   - 《SystemCommand寄存器映射与Domain-Infrastructure对接设计》§4.8.3
//   - 《SystemCommand寄存器映射与Domain-Infrastructure对接——TDD开发步骤》§5
// =============================================================================

#include <gtest/gtest.h>
#include <sstream>
#include "infrastructure/plc/AxisStateDeriver.h"
#include "domain/entity/Axis.h"

using ::plc::deriveAxisState;

// ═══════════════════════════════════════════════════════════════════
// 5.1 参数化测试数据结构
// ═══════════════════════════════════════════════════════════════════

struct StateDeriveParam {
    int16_t d100;          // D100~D103 (0=未使能, 1=使能, 2=运动, 3=报警)
    int16_t alarmCode;     // D110~D113 (0=正常, 非零=报警)
    bool absMoving;        // M110/M113/M116/M119 (绝对定位进行中)
    bool relMoving;        // M111/M114/M117/M120 (相对定位进行中)
    bool jogging;          // M112/M115/M118/M121 (点动进行中)
    AxisState expected;    // 期望的推导结果

    // 辅助：生成可读的测试名称
    std::string description() const {
        std::ostringstream oss;
        oss << "D100=" << d100
            << "_Alarm=" << alarmCode
            << "_ABS=" << (absMoving ? "T" : "F")
            << "_REL=" << (relMoving ? "T" : "F")
            << "_JOG=" << (jogging ? "T" : "F")
            << "->" << axisStateName(expected);
        return oss.str();
    }
};

// ═══════════════════════════════════════════════════════════════════
// 5.2 参数化测试夹具
// ═══════════════════════════════════════════════════════════════════

class DeriveStateParamTest : public ::testing::TestWithParam<StateDeriveParam> {};

// ═══════════════════════════════════════════════════════════════════
// 5.3 参数实例化 —— 15 组全覆盖
// ═══════════════════════════════════════════════════════════════════

INSTANTIATE_TEST_SUITE_P(
    AllSignalCombinations,
    DeriveStateParamTest,
    ::testing::Values(
        // ---------- 基础路径：未使能 ----------
        StateDeriveParam{0, 0, false, false, false, AxisState::Disabled},      // S1
        // ---------- 报警优先于 Disabled ----------
        StateDeriveParam{0, 5, false, false, false, AxisState::Error},         // S2: alarmCode!=0
        StateDeriveParam{0, -1, false, false, false, AxisState::Error},        // S3: alarmCode 负数
        // ---------- 报警优先于所有信号 ----------
        StateDeriveParam{3, 0, false, false, false, AxisState::Error},         // S4: D100=3
        StateDeriveParam{3, 0, true,  false, false, AxisState::Error},         // S5: D100=3 + ABS_M
        StateDeriveParam{3, 7, true,  true,  false, AxisState::Error},         // S6: D100=3 + alarmCode!=0 + 双 Coil
        StateDeriveParam{1, 3, false, false, false, AxisState::Error},         // S14: D100=1 但 alarmCode!=0
        StateDeriveParam{2, 5, true,  false, false, AxisState::Error},         // S15: D100=2 + alarmCode!=0 + ABS_M
        // ---------- 运动子类型：D100=1 ----------
        StateDeriveParam{1, 0, true,  false, false, AxisState::MovingAbsolute},// S7
        StateDeriveParam{1, 0, false, true,  false, AxisState::MovingRelative},// S8
        StateDeriveParam{1, 0, false, false, true,  AxisState::Jogging},       // S9
        StateDeriveParam{1, 0, false, false, false, AxisState::Idle},          // S10
        // ---------- 运动子类型：D100=2 (测试编码器抖跳场景) ----------
        StateDeriveParam{2, 0, false, false, false, AxisState::Idle},          // S11: D100=2 无 Coil -> Idle
        StateDeriveParam{2, 0, true,  false, false, AxisState::MovingAbsolute},// S12
        StateDeriveParam{2, 0, false, true,  false, AxisState::MovingRelative} // S13
    ),
    [](const ::testing::TestParamInfo<StateDeriveParam>& info) {
        // 自定义测试名称，使 GTest 输出可读
        // 格式：D0A0_FFF, D0A5_FFF, ...
        // 注意：GTest 参数化测试名称不允许包含 '-'，
        //       负数用 'N' 前缀表示（如 -1 → N1）
        auto& p = info.param;
        std::ostringstream name;
        name << "D" << p.d100
             << "A";
        if (p.alarmCode < 0) {
            name << "N" << -p.alarmCode;
        } else {
            name << p.alarmCode;
        }
        name << "_" << (p.absMoving ? "T" : "F")
             << (p.relMoving ? "T" : "F")
             << (p.jogging ? "T" : "F");
        return name.str();
    }
);

// ═══════════════════════════════════════════════════════════════════
// 5.4 参数化测试主体
// ═══════════════════════════════════════════════════════════════════

TEST_P(DeriveStateParamTest, DeriveAxisStateCorrectly) {
    const auto& p = GetParam();

    AxisState actual = deriveAxisState(
        p.d100, p.alarmCode,
        p.absMoving, p.relMoving, p.jogging
    );

    EXPECT_EQ(actual, p.expected)
        << "FAIL: " << p.description()
        << "\n  Input:  D100=" << p.d100
        << "  alarmCode=" << p.alarmCode
        << "  absMoving=" << p.absMoving
        << "  relMoving=" << p.relMoving
        << "  jogging=" << p.jogging
        << "\n  Expected: " << axisStateName(p.expected)
        << "\n  Actual:   " << axisStateName(actual);
}

// ═══════════════════════════════════════════════════════════════════
// 5.5 边界场景测试夹具
// ═══════════════════════════════════════════════════════════════════

class DeriveStateEdgeCaseTest : public ::testing::Test {};

// ═══════════════════════════════════════════════════════════════════
// 5.6 补充测试 1：多圈编码器 D100 抖动不误触发运动状态
//
// 背景（设计文档 §4.8.1）：
//   多圈绝对值编码电机使能后，D100 会在 1 和 2 之间短暂跳变。
//   如果依赖 D100 直接判断运动状态，会误判为空闲->运动->空闲的来回跳变。
//
// 验证：
//   - D100=2 时若所有运动 Coil 均为 OFF，返回 Idle（不误判为运动状态）
//   - D100=1 时若 JOGGING ON，返回 Jogging（Coil 信号才是权威来源）
// ═══════════════════════════════════════════════════════════════════

TEST_F(DeriveStateEdgeCaseTest, D100JitterDoesNotFalseTrigger) {
    // 场景 A：D100=2（"运动状态"）但无运动 Coil -> 应为 Idle
    // 这模拟了多圈编码器导致 D100 闪跳为 2 的情况
    EXPECT_EQ(deriveAxisState(2, 0, false, false, false), AxisState::Idle)
        << "D100=2 with no motion Coils should be Idle (encoder jitter)";

    // 场景 B：D100=1 但 JOGGING Coil ON -> 应为 Jogging
    // Coil 信号才是运动状态的权威来源
    EXPECT_EQ(deriveAxisState(1, 0, false, false, true), AxisState::Jogging)
        << "D100=1 with JOGGING ON should be Jogging";

    // 场景 C：D100 在 1<->2 之间来回跳变，但 ABS_MOVING 持续 ON
    // -> 应保持 MovingAbsolute，不受 D100 抖动影响
    EXPECT_EQ(deriveAxisState(1, 0, true, false, false), AxisState::MovingAbsolute)
        << "D100=1 with ABS_MOVING ON should stay MovingAbsolute";
    EXPECT_EQ(deriveAxisState(2, 0, true, false, false), AxisState::MovingAbsolute)
        << "D100=2 with ABS_MOVING ON should also stay MovingAbsolute";
}

// ═══════════════════════════════════════════════════════════════════
// 5.7 补充测试 2：四轴独立推导
//
// 验证同一个 deriveAxisState 函数对不同轴的输入产生正确的独立结果。
// 四条轴的状态完全独立，互不影响。
// ═══════════════════════════════════════════════════════════════════

TEST_F(DeriveStateEdgeCaseTest, AllFourAxesIndependentDerive) {
    // X 轴：使能静止 (D100=1, 无 Coil)
    EXPECT_EQ(deriveAxisState(1, 0, false, false, false), AxisState::Idle);

    // Y 轴：绝对定位进行中 (D100=1, ABS_MOVING ON)
    EXPECT_EQ(deriveAxisState(1, 0, true, false, false), AxisState::MovingAbsolute);

    // Z 轴：未使能 (D100=0)
    EXPECT_EQ(deriveAxisState(0, 0, false, false, false), AxisState::Disabled);

    // R 轴：报警 (D100=3)
    EXPECT_EQ(deriveAxisState(3, 0, false, false, false), AxisState::Error);
}

// ═══════════════════════════════════════════════════════════════════
// 5.8 补充测试 3：plc::deriveAxisState 从不返回 Unknown
//
// AxisState::Unknown 仅用于 Axis 实体的初始值（构造时的占位状态），
// 不应该是 deriveAxisState 的有效返回值。
// ═══════════════════════════════════════════════════════════════════

TEST_F(DeriveStateEdgeCaseTest, NeverReturnsUnknown) {
    // 遍历所有合法 D100 值，确保不返回 Unknown
    for (int16_t d100 : {0, 1, 2, 3}) {
        for (int16_t alarm : {0, 1, -1}) {
            for (bool absM : {false, true}) {
                for (bool relM : {false, true}) {
                    for (bool jog : {false, true}) {
                        AxisState result = deriveAxisState(d100, alarm, absM, relM, jog);
                        EXPECT_NE(result, AxisState::Unknown)
                            << "deriveAxisState should never return Unknown\n"
                            << "  D100=" << d100 << " alarm=" << alarm
                            << " abs=" << absM << " rel=" << relM << " jog=" << jog;
                    }
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// 5.9 补充测试 4：alarmCode 非零时无视其他所有信号
//
// 设计原则：报警是最高优先级。一旦 alarmCode!=0 或 D100=3，
// 无论运动 Coil 如何组合，都必须返回 Error。
// ═══════════════════════════════════════════════════════════════════

TEST_F(DeriveStateEdgeCaseTest, AlarmCodeOverridesEverything) {
    // 所有 D100 值 x 所有运动 Coil 组合下，只要 alarmCode!=0 就返回 Error
    for (int16_t d100 : {0, 1, 2, 3}) {
        for (int16_t alarm : {1, 5, 100, -1, -999}) {
            EXPECT_EQ(deriveAxisState(d100, alarm, true, true, true), AxisState::Error)
                << "alarmCode=" << alarm << " should force Error even with all Coils ON, D100=" << d100;
            EXPECT_EQ(deriveAxisState(d100, alarm, false, false, false), AxisState::Error)
                << "alarmCode=" << alarm << " should force Error even with all Coils OFF, D100=" << d100;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// 5.10 补充测试 5：D100=3 时无视所有其他信号
// ═══════════════════════════════════════════════════════════════════

TEST_F(DeriveStateEdgeCaseTest, D100Equals3OverridesEverything) {
    // D100=3 应始终返回 Error，即使 alarmCode=0
    for (bool absM : {false, true}) {
        for (bool relM : {false, true}) {
            for (bool jog : {false, true}) {
                EXPECT_EQ(deriveAxisState(3, 0, absM, relM, jog), AxisState::Error)
                    << "D100=3 should force Error regardless of Coil states";
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// 5.11 补充测试 6：同时多个运动 Coil ON 的行为（防御性测试）
//
// 虽然 PLC 程序保证运动 Coil 互斥，但作为防御性编程，
// 验证多 Coil 同时 ON 时按优先级链返回第一个匹配的状态。
// ═══════════════════════════════════════════════════════════════════

TEST_F(DeriveStateEdgeCaseTest, MultipleCoilsOnReturnsFirstPriority) {
    // ABS_MOVING + REL_MOVING 同时 ON -> 应返回 MovingAbsolute（优先级最高）
    EXPECT_EQ(deriveAxisState(1, 0, true, true, false), AxisState::MovingAbsolute);

    // ABS_MOVING + JOGGING 同时 ON -> MovingAbsolute
    EXPECT_EQ(deriveAxisState(1, 0, true, false, true), AxisState::MovingAbsolute);

    // REL_MOVING + JOGGING 同时 ON -> MovingRelative
    EXPECT_EQ(deriveAxisState(1, 0, false, true, true), AxisState::MovingRelative);

    // 三个同时 ON -> MovingAbsolute
    EXPECT_EQ(deriveAxisState(2, 0, true, true, true), AxisState::MovingAbsolute);
}
