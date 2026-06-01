# 阶段三：状态推导引擎单元测试 —— TDD 详细开发文档

> 版本：v1.0  
> 日期：2026-05-30  
> 项目：servoV6  
> 前置文档：
> - [SystemCommand 寄存器映射与 Domain-Infrastructure 对接设计 (v4.0)](./SystemCommand寄存器映射与Domain-Infrastructure对接设计.md)
> - [SystemCommand 寄存器映射与 Domain-Infrastructure 对接 —— TDD 开发步骤 (v2.0)](./SystemCommand寄存器映射与Domain-Infrastructure对接——TDD开发步骤.md)

---

## 目录

1. [目标与范围](#1-目标与范围)
2. [架构决策：纯函数提取](#2-架构决策纯函数提取)
3. [推导规则规范](#3-推导规则规范)
4. [测试文件规划](#4-测试文件规划)
5. [RED 步骤 —— 测试用例编写](#5-red-步骤--测试用例编写)
6. [GREEN 步骤 —— 生产代码实现](#6-green-步骤--生产代码实现)
7. [REFACTOR 步骤](#7-refactor-步骤)
8. [CMakeLists 变更](#8-cmakelists-变更)
9. [构建与运行命令](#9-构建与运行命令)
10. [验收检查清单](#10-验收检查清单)

---

## 1. 目标与范围

### 1.1 阶段目标

验证 `deriveAxisState()` 方法的多信号融合逻辑，覆盖所有 D100 状态值、ALARM_CODE 与 Coil 信号的组合。本阶段聚焦于**状态推导引擎本身的正确性**，不涉及 `ModbusSystemDriver` 的完整 `pollFeedback` 链路（那是阶段五的职责）。

### 1.2 测试范围

| 范围 | 包含 | 不包含 |
|------|------|--------|
| D100 状态码 | 0 (未使能)、1 (使能)、2 (运动)、3 (报警) | 其他非法值 |
| ALARM_CODE | 0 (无报警)、正值、负值 | — |
| 运动 Coil | ABS_MOVING (M110/M113/M116/M119)、REL_MOVING (M111/M114/M117/M120)、JOGGING (M112/M115/M118/M121) | MOVE_DONE (M100~M103) — 不参与状态推导 |
| 优先级链 | Error → Disabled → 运动子类型 → Idle | — |
| 多圈编码器抖动 | D100 在 1↔2 之间抖跳的场景 | — |
| 四轴独立推导 | X/Y/Z/R 四条轴对同一推导函数的不同输入 | — |

### 1.3 当前代码状态

```
已有资产 ✅
├── domain/entity/Axis.h           — AxisState 枚举 (Unknown/Disabled/Idle/Jogging/MovingAbsolute/MovingRelative/Error)
├── domain/entity/Axis.h           — axisStateName() 辅助函数
├── infrastructure/plc/ModbusSystemDriver.h  — regFbState / regFbAlarmCode / regFbAbsMoving / regFbRelMoving / regFbJogging 选择器
├── infrastructure/plc/protocol/PlcDevice.h   — readInt16 / readBool 方法
├── infrastructure/utils/IClock.h             — SteadyClock / FakeClock

待开发 ❌
├── infrastructure/plc/AxisStateDeriver.h     — 纯函数 deriveAxisState()
├── tests/infrastructure/test_modbus_system_driver_state_derive.cpp  — 本阶段的测试文件
└── tests/CMakeLists.txt                      — 新增 test target
```

---

## 2. 架构决策：纯函数提取

### 2.1 决策

将 `deriveAxisState` 从 `ModbusSystemDriver` 的 private 方法提取为**独立 free function**，放在新文件 `infrastructure/plc/AxisStateDeriver.h` 中。

### 2.2 决策理由

| 因素 | 嵌入 ModbusSystemDriver 内部 | 提取为独立 free function |
|------|---------------------------|-------------------------|
| **测试独立性** | 需要构造 ModbusSystemDriver 实例（含 PlcDevice、IClock 等成员） | 纯函数，5 个参数直传，无任何依赖 |
| **可重用性** | 绑定到特定 Driver 类，无法被其他组件使用 | 可用于 FakeAxisDriver、日志、诊断等任何需要状态推导的地方 |
| **职责单一** | Driver 承担了过多的职责 | 符合 SRP：Driver 负责通讯，Deriver 负责信号融合 |
| **编译速度** | 修改推导逻辑触发 ModbusSystemDriver.h 重编译所有依赖方 | 仅头文件，影响范围最小 |
| **测试工具** | 需要 GTest fixture + MockPlcDevice | 直接调用 free function + EXPECT_EQ，零 mock |

### 2.3 函数签名

```cpp
// 文件：infrastructure/plc/AxisStateDeriver.h
#pragma once

#include "domain/entity/Axis.h"  // AxisState
#include <cstdint>

namespace plc {

/// @brief 多信号融合：将 PLC 离散信号推导为 Domain 统一 AxisState
///
/// 输入参数来自 PLC 不同寄存器：
///   - d100State  : D100~D103 (STATE) — Int16, 0=未使能/1=使能/2=运动/3=报警
///   - alarmCode  : D110~D113 (ALARM_CODE) — Int16, 0=正常/非零=报警码
///   - absMoving  : M110/M113/M116/M119 (ABS_MOVING) — Bool
///   - relMoving  : M111/M114/M117/M120 (REL_MOVING) — Bool
///   - jogging    : M112/M115/M118/M121 (JOGGING) — Bool
///
/// 推导优先级链（设计文档 §4.8.3）：
///   1. d100State == 3 || alarmCode != 0  → AxisState::Error
///   2. d100State == 0                    → AxisState::Disabled
///   3. absMoving                         → AxisState::MovingAbsolute
///   4. relMoving                         → AxisState::MovingRelative
///   5. jogging                           → AxisState::Jogging
///   6. else                              → AxisState::Idle
///
/// @note 此函数为纯函数，不依赖任何外部状态，可在任何线程、任何时刻调用
AxisState deriveAxisState(int16_t d100State,
                          int16_t alarmCode,
                          bool absMoving,
                          bool relMoving,
                          bool jogging);

} // namespace plc
```

### 2.4 在 ModbusSystemDriver 中的集成

```cpp
// ModbusSystemDriver 的 readAxisFeedback 中调用：

AxisState ModbusSystemDriver::deriveAxisStateForAxis(AxisId id) const {
    return deriveAxisState(
        m_device->readInt16(regFbState(id)),      // D100~D103
        m_device->readInt16(regFbAlarmCode(id)),   // D110~D113
        m_device->readBool(regFbAbsMoving(id)),     // M110/M113/M116/M119
        m_device->readBool(regFbRelMoving(id)),     // M111/M114/M117/M120
        m_device->readBool(regFbJogging(id))        // M112/M115/M118/M121
    );
}
```

> 注意：`ModbusSystemDriver` 的内部方法命名为 `deriveAxisStateForAxis(AxisId)` 以避免与 free function `deriveAxisState(int16_t, int16_t, bool, bool, bool)` 的符号冲突。

---

## 3. 推导规则规范

### 3.1 正式规则（设计文档 §4.8.3）

```
输入: d100State ∈ {0, 1, 2, 3}, alarmCode ∈ ℤ, absMoving ∈ {true, false},
      relMoving ∈ {true, false}, jogging ∈ {true, false}

推导（严格按优先级判断）:
  IF (d100State == 3) OR (alarmCode != 0)
    → AxisState::Error           // 最高优先级：报警

  IF (d100State == 0)
    → AxisState::Disabled        // 次优先级：未使能

  // 以下 d100State ∈ {1, 2}：使能状态，由 Coil 信号决定精确子状态
  IF (absMoving)
    → AxisState::MovingAbsolute

  IF (relMoving)
    → AxisState::MovingRelative

  IF (jogging)
    → AxisState::Jogging

  ELSE
    → AxisState::Idle            // 使能但无任何运动信号
```

### 3.2 关键设计原理

1. **报警最高优先级（双条件 OR）**：`d100State == 3` 是 PLC 报警的标准状态码，`alarmCode != 0` 作为冗余兜底。两个条件任一满足即判定 Error。PLC 报警时会将运动 Coil 全部清零，此规则确保 Error 不被误判为 Idle。

2. **Disabled 由 D100=0 判定**：未使能状态下所有运动 Coil 均 OFF，D100=0 是可靠信号。但需注意：如果 `alarmCode != 0` 且 `d100State == 0`，Error 优先于 Disabled（见 S2/S3 用例）。

3. **D100 的 1↔2 抖跳被消除**：使能后 D100 不论是 1 还是 2，都由独立的 Coil 信号决定子状态。多圈绝对值编码器导致的 2 闪烁不会误触发运动状态判断（见 §5.3 抖动场景测试）。

4. **运动 Coil 互斥假设**：PLC 程序保证同一时刻最多一个运动 Coil 为 ON，因此 if-else 链安全。

5. **MOVE_DONE 不参与推导**：`MOVE_DONE` (M100~M103) 是运动完成后的暂态信号，可能短暂为 OFF 导致误判。状态推导只用"正在运动中"的持续信号（ABS_MOVING / REL_MOVING / JOGGING）。

6. **Unknown 不作为推导结果**：推导引擎只返回可观测的确定状态，`AxisState::Unknown` 仅用作 `Axis` 实体的初始值（构造时的占位状态）。PLC 只要连接正常，一定处于 0/1/2/3 之一。

---

## 4. 测试文件规划

### 4.1 文件命名

```
tests/infrastructure/test_modbus_system_driver_state_derive.cpp
```

命名遵循 `test_modbus_system_driver_*` 前缀约定（TDD 步骤文档 §2），与阶段一的 `test_modbus_system_driver_registers.cpp` 和阶段二的 `test_modbus_system_driver_edge_trigger.cpp` 保持一致。

### 4.2 测试架构

```
测试文件结构：
├── DeriveAxisStateParamTest       (TEST_P 参数化测试 — 15 项)
│   └── 覆盖全部 D100 × AlarmCode × Coil 组合
├── DeriveAxisStateEdgeCaseTest    (TEST_F 夹具 — 补充场景)
│   ├── D100JitterDoesNotFalseTrigger
│   └── AllFourAxesIndependentDerive
└── main()                         (标准 GTest entry point)
```

因为 `deriveAxisState` 是纯函数，**不需要任何 fixture 成员**——参数化测试直接传递 5 个值，`TEST_F` 也不需要 SetUp。

### 4.3 依赖关系

```
test_modbus_system_driver_state_derive
  ├── infrastructure/plc/AxisStateDeriver.h      (待 GREEN 阶段创建)
  ├── domain/entity/Axis.h                       (已有 — AxisState 枚举)
  └── gtest_main                                 (已有 — external/googletest)
```

不需要 mock 任何对象，不需要 PlcDevice、ModbusSystemDriver、IClock 等任何基础设施。

---

## 5. RED 步骤 —— 测试用例编写

> ⚠️ **TDD 规范**：此步骤编写的测试文件必须先编译通过但运行失败（因为 `deriveAxisState` 尚未实现），然后再在 GREEN 步骤中实现函数使测试通过。

### 5.1 测试用例完整清单

| 编号 | D100 | AlarmCode | ABS_M | REL_M | JOG | 预期 AxisState | 分类 | 注释 |
|------|------|-----------|-------|-------|-----|----------------|------|------|
| S1 | 0 | 0 | F | F | F | **Disabled** | 基础路径 | 未使能，无报警 |
| S2 | 0 | 5 | F | F | F | **Error** | 报警优先 | alarmCode≠0 优先于 Disabled |
| S3 | 0 | -1 | F | F | F | **Error** | 报警优先 | alarmCode 负数也判定 Error |
| S4 | 3 | 0 | F | F | F | **Error** | 报警优先 | D100=3 是 PLC 标准报警码 |
| S5 | 3 | 0 | T | F | F | **Error** | 报警优先 | Error 高于运动 Coil |
| S6 | 3 | 7 | T | T | F | **Error** | 报警优先 | D100=3 且 alarmCode≠0 双重确认 |
| S7 | 1 | 0 | T | F | F | **MovingAbsolute** | 运动子类型 | 绝对定位进行中 |
| S8 | 1 | 0 | F | T | F | **MovingRelative** | 运动子类型 | 相对定位进行中 |
| S9 | 1 | 0 | F | F | T | **Jogging** | 运动子类型 | 点动进行中 |
| S10 | 1 | 0 | F | F | F | **Idle** | 基础路径 | 使能静止 |
| S11 | 2 | 0 | F | F | F | **Idle** | 抖动消除 | D100=2 但无运动 Coil（编码器抖跳） |
| S12 | 2 | 0 | T | F | F | **MovingAbsolute** | 运动子类型 | D100=2 + ABS_MOVING 正常运动 |
| S13 | 2 | 0 | F | T | F | **MovingRelative** | 运动子类型 | D100=2 + REL_MOVING 正常运动 |
| S14 | 1 | 3 | F | F | F | **Error** | 报警优先 | D100=1 但 alarmCode≠0（报警码先到达） |
| S15 | 2 | 5 | T | F | F | **Error** | 报警优先 | 报警码优先级最高，覆盖所有运动信号 |

### 5.2 测试文件完整内容

**文件**：`tests/infrastructure/test_modbus_system_driver_state_derive.cpp`

```cpp
// =============================================================================
// TDD 阶段三：状态推导引擎单元测试
//
// 测试对象：plc::deriveAxisState() 纯函数
// 覆盖范围：15 项参数化组合 + 2 项边界场景
//
// 设计依据：
//   - 《SystemCommand寄存器映射与Domain-Infrastructure对接设计》§4.8.3
//   - 《SystemCommand寄存器映射与Domain-Infrastructure对接——TDD开发步骤》§5
// =============================================================================

#include <gtest/gtest.h>
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
            << "→" << axisStateName(expected);
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
        // ────────── 基础路径：未使能 ──────────
        StateDeriveParam{0, 0, false, false, false, AxisState::Disabled},      // S1
        // ────────── 报警优先于 Disabled ──────────
        StateDeriveParam{0, 5, false, false, false, AxisState::Error},         // S2: alarmCode≠0
        StateDeriveParam{0, -1, false, false, false, AxisState::Error},        // S3: alarmCode 负数
        // ────────── 报警优先于所有信号 ──────────
        StateDeriveParam{3, 0, false, false, false, AxisState::Error},         // S4: D100=3
        StateDeriveParam{3, 0, true,  false, false, AxisState::Error},         // S5: D100=3 + ABS_M
        StateDeriveParam{3, 7, true,  true,  false, AxisState::Error},         // S6: D100=3 + alarmCode≠0 + 双 Coil
        StateDeriveParam{1, 3, false, false, false, AxisState::Error},         // S14: D100=1 但 alarmCode≠0
        StateDeriveParam{2, 5, true,  false, false, AxisState::Error},         // S15: D100=2 + alarmCode≠0 + ABS_M
        // ────────── 运动子类型：D100=1 ──────────
        StateDeriveParam{1, 0, true,  false, false, AxisState::MovingAbsolute},// S7
        StateDeriveParam{1, 0, false, true,  false, AxisState::MovingRelative},// S8
        StateDeriveParam{1, 0, false, false, true,  AxisState::Jogging},       // S9
        StateDeriveParam{1, 0, false, false, false, AxisState::Idle},          // S10
        // ────────── 运动子类型：D100=2 (测试编码器抖跳场景) ──────────
        StateDeriveParam{2, 0, false, false, false, AxisState::Idle},          // S11: D100=2 无 Coil → Idle
        StateDeriveParam{2, 0, true,  false, false, AxisState::MovingAbsolute},// S12
        StateDeriveParam{2, 0, false, true,  false, AxisState::MovingRelative} // S13
    ),
    [](const ::testing::TestParamInfo<StateDeriveParam>& info) {
        // 自定义测试名称，使 GTest 输出可读
        // 格式：D100X_AlarmX_ABSx_RELx_JOGx
        auto& p = info.param;
        std::ostringstream name;
        name << "D" << p.d100
             << "A" << p.alarmCode
             << "_" << (p.absMoving ? "T" : "F")
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
//   如果依赖 D100 直接判断运动状态，会误判为空闲→运动→空闲的来回跳变。
//
// 验证：
//   - D100=2 时若所有运动 Coil 均为 OFF，返回 Idle（不误判为运动状态）
//   - D100=1 时若 JOGGING ON，返回 Jogging（Coil 信号才是权威来源）
// ═══════════════════════════════════════════════════════════════════

TEST_F(DeriveStateEdgeCaseTest, D100JitterDoesNotFalseTrigger) {
    // 场景 A：D100=2（"运动状态"）但无运动 Coil → 应为 Idle
    // 这模拟了多圈编码器导致 D100 闪跳为 2 的情况
    EXPECT_EQ(deriveAxisState(2, 0, false, false, false), AxisState::Idle)
        << "D100=2 with no motion Coils should be Idle (encoder jitter)";

    // 场景 B：D100=1 但 JOGGING Coil ON → 应为 Jogging
    // Coil 信号才是运动状态的权威来源
    EXPECT_EQ(deriveAxisState(1, 0, false, false, true), AxisState::Jogging)
        << "D100=1 with JOGGING ON should be Jogging";

    // 场景 C：D100 在 1↔2 之间来回跳变，但 ABS_MOVING 持续 ON
    // → 应保持 MovingAbsolute，不受 D100 抖动影响
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
// 设计原则：报警是最高优先级。一旦 alarmCode≠0 或 D100=3，
// 无论运动 Coil 如何组合，都必须返回 Error。
// ═══════════════════════════════════════════════════════════════════

TEST_F(DeriveStateEdgeCaseTest, AlarmCodeOverridesEverything) {
    // 所有 D100 值 × 所有运动 Coil 组合下，只要 alarmCode≠0 就返回 Error
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
    // ABS_MOVING + REL_MOVING 同时 ON → 应返回 MovingAbsolute（优先级最高）
    EXPECT_EQ(deriveAxisState(1, 0, true, true, false), AxisState::MovingAbsolute);

    // ABS_MOVING + JOGGING 同时 ON → MovingAbsolute
    EXPECT_EQ(deriveAxisState(1, 0, true, false, true), AxisState::MovingAbsolute);

    // REL_MOVING + JOGGING 同时 ON → MovingRelative
    EXPECT_EQ(deriveAxisState(1, 0, false, true, true), AxisState::MovingRelative);

    // 三个同时 ON → MovingAbsolute
    EXPECT_EQ(deriveAxisState(2, 0, true, true, true), AxisState::MovingAbsolute);
}

// ═══════════════════════════════════════════════════════════════════
// GTest main
// ═══════════════════════════════════════════════════════════════════

// 如果与其他测试 target 合并链接，可省略 main；
// 作为独立 target 时需要显式提供。
// gtest_main 库已提供 main()，此处无需重复。
```

### 5.3 测试用例设计原理说明

#### 5.3.1 参数化 15 项的覆盖逻辑

| 分类 | 用例编号 | 核心验证点 |
|------|---------|-----------|
| **基础路径** | S1, S10 | 最简路径：Disabled 和 Idle |
| **报警优先** | S2-S6, S14-S15 | Error 比 Disabled 优先（S2/S3）；Error 比运动 Coil 优先（S5/S6/S15）；D100=3 和 alarmCode≠0 各自独立触发 Error（S4/S14） |
| **运动子类型** | S7-S9, S12-S13 | D100=1 和 D100=2 下三种运动 Coil 各自正确映射到对应 AxisState |
| **抖动消除** | S11 | D100=2（"运动态"）但无任何运动 Coil → 不误判 |
| **双重确认** | S6 | D100=3 且 alarmCode≠0 → 仍然 Error，不会因为条件冗余而出错 |

#### 5.3.2 补充场景的覆盖逻辑

| 测试 | 覆盖的风险 |
|------|-----------|
| `D100JitterDoesNotFalseTrigger` | 多圈编码器导致的 D100 抖跳被 Coil 信号消除，回归测试 D100=1 和 D100=2 的一致性 |
| `AllFourAxesIndependentDerive` | 函数对四条轴分别独立推导，无跨轴状态污染 |
| `NeverReturnsUnknown` | 穷举 4×3×2×2×2 = 96 种组合，确保 deriveAxisState 绝不返回 Unknown |
| `AlarmCodeOverridesEverything` | alarmCode≠0 时无视 D100 与 Coil，全路径回归 Error 最高优先级 |
| `D100Equals3OverridesEverything` | D100=3 且 alarmCode=0 时，无视 Coil 组合，全路径回归 Error 最高优先级 |
| `MultipleCoilsOnReturnsFirstPriority` | 防御性测试：多 Coil 同时 ON 时按 absMoving > relMoving > jogging 优先级取首个匹配 |

#### 5.3.3 覆盖率指标

| 维度 | 目标 | 实现方式 |
|------|------|---------|
| **行覆盖** | 100% | if-else 链每条分支均被触发 |
| **条件覆盖** | 100% | `d100State == 3` 和 `alarmCode != 0` 各自独立为 true/false |
| **路径覆盖** | 关键路径 100% | 5 条 if 分支 × 每种 AxisState 出口 |
| **边界值** | D100 全合法域 | 0, 1, 2, 3 四种值全覆盖 |
| **等价类** | alarmCode | 零值、正值、负值 |

---

## 6. GREEN 步骤 —— 生产代码实现

> TDD 规范：此步骤实现 `deriveAxisState()` 使 §5 的全部测试通过。

### 6.1 创建 AxisStateDeriver.h

**文件**：`infrastructure/plc/AxisStateDeriver.h`

```cpp
// =============================================================================
// AxisStateDeriver — 多信号融合状态推导引擎 (header-only)
//
// 职责：将 PLC 多个寄存器/Coil 的离散信号融合为 Domain 统一的 AxisState
//
// 设计原则：
//   1. 纯函数 — 无状态、无副作用、无依赖注入，仅依赖输入参数
//   2. 确定性 — 相同输入永远产生相同输出
//   3. 优先链 — 严格按 Error → Disabled → MovingAbsolute → MovingRelative
//                → Jogging → Idle 优先级判断
//
// 设计依据：
//   - 《SystemCommand寄存器映射与Domain-Infrastructure对接设计》§4.8.3
//   - 《阶段三-状态推导引擎TDD详细开发文档》§3
// =============================================================================

#pragma once

#include "domain/entity/Axis.h"
#include <cstdint>

namespace plc {

/**
 * @brief 多信号融合：将 PLC 离散信号推导为 Domain 统一 AxisState
 *
 * 输入参数来自 PLC 不同寄存器：
 *   - d100State  : D100~D103 (STATE) — Int16
 *                  0=未使能, 1=使能(静止), 2=使能(运动), 3=报警
 *   - alarmCode  : D110~D113 (ALARM_CODE) — Int16
 *                  0=正常, 非零=报警码
 *   - absMoving  : M110/M113/M116/M119 (ABS_MOVING) — Bool
 *   - relMoving  : M111/M114/M117/M120 (REL_MOVING) — Bool
 *   - jogging    : M112/M115/M118/M121 (JOGGING) — Bool
 *
 * 推导优先级链：
 *   1. d100State == 3 || alarmCode != 0  → AxisState::Error
 *   2. d100State == 0                    → AxisState::Disabled
 *   3. absMoving                         → AxisState::MovingAbsolute
 *   4. relMoving                         → AxisState::MovingRelative
 *   5. jogging                           → AxisState::Jogging
 *   6. else                              → AxisState::Idle
 *
 * @note 此函数为纯函数，不依赖任何外部状态。
 *       PLC 连接正常时，d100State 一定在 {0,1,2,3} 之内。
 *
 * @return 推导出的 AxisState，永远不会返回 AxisState::Unknown
 */
inline AxisState deriveAxisState(int16_t d100State,
                                 int16_t alarmCode,
                                 bool absMoving,
                                 bool relMoving,
                                 bool jogging) {
    // 优先级 1：报警（最高优先级）
    //   d100State == 3 是 PLC 标准报警状态码
    //   alarmCode != 0 作为冗余兜底，两个条件 OR 关系
    if (d100State == 3 || alarmCode != 0) {
        return AxisState::Error;
    }

    // 优先级 2：未使能
    if (d100State == 0) {
        return AxisState::Disabled;
    }

    // 以下 d100State ∈ {1, 2}：使能状态
    // 由独立的 Coil 信号决定精确子状态
    // 多圈编码器抖跳导致的 D100=1↔2 变化被此 if-else 链消除
    if (absMoving) {
        return AxisState::MovingAbsolute;
    }
    if (relMoving) {
        return AxisState::MovingRelative;
    }
    if (jogging) {
        return AxisState::Jogging;
    }

    // 使能但无任何运动信号
    return AxisState::Idle;
}

} // namespace plc
```

### 6.2 实现要点

1. **inline 关键字**：函数定义在头文件中，使用 `inline` 避免多重定义链接错误。纯函数体极短（<20 行），内联不会造成代码膨胀。

2. **注释对应测试用例**：每个 if 分支的注释明确对应 TDD 文档中的测试编号，便于追溯。

3. **不再判断 Unknown**：函数永远不返回 `AxisState::Unknown`，对应补充测试 `NeverReturnsUnknown`。

4. **D100 抖跳消除机制**：`d100State==1` 和 `d100State==2` 走完全相同的 Coil 判断路径，因此 D100 的 1↔2 抖动不影响最终推导结果。

5. **Coil 互斥防御**：即使多个 Coil 同时 ON（违反 PLC 程序假设），按 `absMoving > relMoving > jogging` 优先级返回第一个匹配项。这是防御性设计，不依赖 PLC 程序的正确性。

### 6.3 预期测试结果（编译并运行）

```
[ RUN      ] AllSignalCombinations/DeriveStateParamTest.DeriveAxisStateCorrectly/D0A0_FF
[       OK ] AllSignalCombinations/DeriveStateParamTest.DeriveAxisStateCorrectly/D0A0_FF
[ RUN      ] AllSignalCombinations/DeriveStateParamTest.DeriveAxisStateCorrectly/D0A5_FF
...
[ RUN      ] AllSignalCombinations/DeriveStateParamTest.DeriveAxisStateCorrectly/D2A0_FT
[       OK ] AllSignalCombinations/DeriveStateParamTest.DeriveAxisStateCorrectly/D2A0_FT
[----------] 15 tests from AllSignalCombinations/DeriveStateParamTest (total)
[ RUN      ] DeriveStateEdgeCaseTest.D100JitterDoesNotFalseTrigger
[       OK ] DeriveStateEdgeCaseTest.D100JitterDoesNotFalseTrigger
[ RUN      ] DeriveStateEdgeCaseTest.AllFourAxesIndependentDerive
[       OK ] DeriveStateEdgeCaseTest.AllFourAxesIndependentDerive
[ RUN      ] DeriveStateEdgeCaseTest.NeverReturnsUnknown
[       OK ] DeriveStateEdgeCaseTest.NeverReturnsUnknown
[ RUN      ] DeriveStateEdgeCaseTest.AlarmCodeOverridesEverything
[       OK ] DeriveStateEdgeCaseTest.AlarmCodeOverridesEverything
[ RUN      ] DeriveStateEdgeCaseTest.D100Equals3OverridesEverything
[       OK ] DeriveStateEdgeCaseTest.D100Equals3OverridesEverything
[ RUN      ] DeriveStateEdgeCaseTest.MultipleCoilsOnReturnsFirstPriority
[       OK ] DeriveStateEdgeCaseTest.MultipleCoilsOnReturnsFirstPriority
[----------] 6 tests from DeriveStateEdgeCaseTest (total)

[  PASSED  ] 21 tests.
```

---

## 7. REFACTOR 步骤

### 7.1 本阶段的重构要点

由于 `deriveAxisState` 是纯函数（body < 20 行），结构已经足够简洁，本阶段不需要大规模重构。但仍需检查以下几点：

| 检查项 | 状态 | 操作 |
|--------|------|------|
| 命名一致性 | `deriveAxisState` vs `DeriveAxisState` 参数化夹具名 | ✅ 一致 |
| 头文件依赖 | AxisStateDeriver.h 仅依赖 `domain/entity/Axis.h` | ✅ 最小依赖 |
| 函数应与 ModbusSystemDriver 解耦 | free function 在独立头文件中 | ✅ 已解耦 |
| gtest 链接 | 测试 target 需链接 `gtest_main` | 待 CMakeLists 确认 |

### 7.2 后续阶段可做但非必须的重构

以下内容可在阶段五（完整 `pollFeedback` 集成测试）实施：

1. **在 ModbusSystemDriver 中集成**：添加 `deriveAxisStateForAxis(AxisId)` 内部方法，委托给纯函数。此重构应延迟到阶段五以确保有集成测试覆盖。
2. **性能优化**：当前 if-else 链在 -O2 优化下已经极快，无需查表优化。若将来添加更多状态值（如 Homing），可考虑将 D100 查表 + Coil 判断结合。
3. **添加日志**：在 `deriveAxisState` 内部添加 LOG_TRACE 记录输入和输出，仅在 debug 模式下启用。此功能应独立于推导逻辑本身，通过装饰器或调用方实现。

### 7.3 不引入的重构

以下曾经被考虑但明确排除：

| 方案 | 排除理由 |
|------|---------|
| 使用 `switch(d100State)` 替代 if-else | alarmCode 检查在 d100State 判断之前，switch 无法表达此优先级 |
| 将 Coil 信号打包为 bitmask | 增加复杂度，减少可读性，对性能无实质提升 |
| 使用 `std::optional<AxisState>` 返回 | 增加调用方复杂度，Unknown 状态已通过枚举覆盖 |
| 将函数放入 `ModbusSystemDriver` 作为 static 方法 | 违反 SRP，增加耦合，不便独立测试 |

---

## 8. CMakeLists 变更

### 8.1 文件：`tests/CMakeLists.txt`

在现有 test targets 之后追加：

```cmake
# ═══════════════════════════════════════════════════════════════════
# 阶段三：状态推导引擎单元测试
# ═══════════════════════════════════════════════════════════════════
add_executable(test_modbus_system_driver_state_derive
    infrastructure/test_modbus_system_driver_state_derive.cpp
)

target_link_libraries(test_modbus_system_driver_state_derive PRIVATE
    gtest_main
)

target_include_directories(test_modbus_system_driver_state_derive PRIVATE
    ${PROJECT_SOURCE_DIR}
)

# 注册到 CTest
add_test(
    NAME ModbusSystemDriver.StateDerive
    COMMAND test_modbus_system_driver_state_derive
)
```

### 8.2 依赖说明

```
test_modbus_system_driver_state_derive
  ├── gtest_main                    (链接)
  ├── ${PROJECT_SOURCE_DIR}         (include — 使 #include "infrastructure/plc/AxisStateDeriver.h" 可解析)
  └── (隐含) domain/entity/Axis.h   (被 AxisStateDeriver.h 包含)
```

**注意**：此 target 不需要链接 `infrastructure` 库、不需要 mock、不需要 FakeClock。纯头文件即可编译。

---

## 9. 构建与运行命令

### 9.1 构建

```bash
# 在 build 目录下执行
cd build
cmake .. -G "MinGW Makefiles"
cmake --build . --target test_modbus_system_driver_state_derive
```

### 9.2 运行

```bash
# 运行全部状态推导测试
ctest -R ModbusSystemDriver.StateDerive --output-on-failure

# 或直接运行可执行文件
./tests/test_modbus_system_driver_state_derive

# 只运行参数化测试
./tests/test_modbus_system_driver_state_derive --gtest_filter="*ParamTest*"

# 只运行边界场景
./tests/test_modbus_system_driver_state_derive --gtest_filter="*EdgeCaseTest*"

# 运行特定报警优先测试
./tests/test_modbus_system_driver_state_derive --gtest_filter="*AlarmCodeOverridesEverything*"
```

### 9.3 TDD 完整流程

```
步骤 1 (RED)：   编写测试文件 → 编译 → 运行 → 全部 FAIL ❌
                  (因为 AxisStateDeriver.h 尚未包含实现或函数体为 stub)

步骤 2 (GREEN)： 实现 deriveAxisState() 函数体 → 编译 → 运行 → 21/21 PASS ✅

步骤 3 (REFACTOR)：检查命名/依赖/结构 → 运行 → 21/21 PASS ✅
                   (确认重构未破坏任何测试)
```

---

## 10. 验收检查清单

### 10.1 必须通过的检查

- [ ] **测试文件创建**：`tests/infrastructure/test_modbus_system_driver_state_derive.cpp` 文件已创建
- [ ] **生产代码创建**：`infrastructure/plc/AxisStateDeriver.h` 文件已创建
- [ ] **CMakeLists 更新**：`tests/CMakeLists.txt` 已添加 `test_modbus_system_driver_state_derive` target
- [ ] **编译通过**：`cmake --build . --target test_modbus_system_driver_state_derive` 无错误
- [ ] **参数化测试全通过**：15/15 项 `DeriveStateParamTest.DeriveAxisStateCorrectly` PASS
- [ ] **抖动场景通过**：`DeriveStateEdgeCaseTest.D100JitterDoesNotFalseTrigger` PASS
- [ ] **四轴独立通过**：`DeriveStateEdgeCaseTest.AllFourAxesIndependentDerive` PASS
- [ ] **不返回 Unknown**：`DeriveStateEdgeCaseTest.NeverReturnsUnknown` PASS（96 种组合）
- [ ] **报警优先通过**：`DeriveStateEdgeCaseTest.AlarmCodeOverridesEverything` PASS
- [ ] **D100=3 优先通过**：`DeriveStateEdgeCaseTest.D100Equals3OverridesEverything` PASS
- [ ] **多 Coil 防御通过**：`DeriveStateEdgeCaseTest.MultipleCoilsOnReturnsFirstPriority` PASS
- [ ] **总测试数**：21 tests PASSED，0 FAILED

### 10.2 代码质量检查

- [ ] `deriveAxisState()` 为 `inline` 纯函数，无状态、无副作用
- [ ] 函数注释完整体现优先级链规则
- [ ] 头文件依赖最小：仅 `domain/entity/Axis.h` 和 `<cstdint>`
- [ ] 测试文件命名遵循 `test_modbus_system_driver_*` 前缀约定
- [ ] 参数化测试使用自定义名称打印器，失败时可快速定位用例
- [ ] 不返回 `AxisState::Unknown`（由 `NeverReturnsUnknown` 穷举验证）

### 10.3 回归检查

- [ ] 已有测试不受影响：`ctest` 全量运行，阶段一和阶段二的测试保持 PASS
- [ ] 阶段一（`test_modbus_system_driver_registers`）不受影响
- [ ] 阶段二（`test_modbus_system_driver_edge_trigger`）不受影响

### 10.4 文档检查

- [ ] 本 TDD 文档已放置在 `docs/architecture/` 目录下
- [ ] 文档中所有代码示例与实际文件一致
- [ ] 测试用例编号 S1~S15 与参数化列表一一对应
- [ ] 优先级链规则与设计文档 §4.8.3 一致

---

## 附录 A：与其他阶段的接口约定

### A.1 阶段四依赖（pollFeedback 集成）

阶段五（原计划阶段四）将实现 `ModbusSystemDriver::pollFeedback()` 完整链路。届时：

- `AxisStateDeriver.h` 中的 `deriveAxisState()` 纯函数直接复用，无需修改
- `ModbusSystemDriver` 通过内部 `deriveAxisStateForAxis(AxisId)` 方法委托给纯函数
- 阶段五的测试可使用 FakePLC 设置特定 D100/AlarmCode/Coil 值，验证完整 `pollFeedback` 链路中的状态推导结果

### A.2 阶段依赖图

```
阶段一 (寄存器注册) ─── 已完成 ───┐
                                   │
阶段二 (边沿触发)    ─── 已完成 ───┤
                                   ├──→ 阶段五 (pollFeedback 集成)
阶段三 (状态推导)    ─── 本阶段 ───┤
                                   │
阶段四 (命令发送)    ─── 待开发 ───┘
```

### A.3 与 FakeAxisDriver 的关系

`FakeAxisDriver` (`infrastructure/FakeAxisDriver.h`) 也可以复用 `plc::deriveAxisState()`：

```cpp
// FakeAxisDriver 中模拟 PLC 反馈：
AxisState state = plc::deriveAxisState(
    m_fakeD100, m_fakeAlarmCode,
    m_fakeAbsMoving, m_fakeRelMoving, m_fakeJogging
);
```

这确保了 Fake 和真实 PLC 使用完全相同的状态推导逻辑，避免测试环境和生产环境行为不一致。

---

## 附录 B：AxisState 枚举参考

（来自 `domain/entity/Axis.h`，仅供参考）

```cpp
enum class AxisState {
    Unknown,         // 初始状态 / 未连接
    Disabled,        // 未使能（伺服未上电）
    Idle,            // 使能静止
    Jogging,         // 点动中
    MovingAbsolute,  // 绝对定位中
    MovingRelative,  // 相对定位中
    Error            // 报警状态
};
```

---

> **文档结束**  
> 下一步：将本阶段的测试文件和 `AxisStateDeriver.h` 按 TDD 三步法（RED → GREEN → REFACTOR）实际编写并验证通过。
