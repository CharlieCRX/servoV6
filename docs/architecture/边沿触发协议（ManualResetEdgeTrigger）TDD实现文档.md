# 边沿触发协议（ManualResetEdgeTrigger）TDD 实现文档

> 版本：v1.0  
> 日期：2026-05-30  
> 项目：servoV6  
> 前置设计：[SystemCommand 寄存器映射与 Domain-Infrastructure 对接设计](./SystemCommand寄存器映射与Domain-Infrastructure对接设计.md)  
> 关联 TDD：[SystemCommand 寄存器映射与 Domain-Infrastructure 对接 —— TDD 开发步骤](./SystemCommand寄存器映射与Domain-Infrastructure对接——TDD开发步骤.md)

---

## 目录

1. [问题定义：什么是 ManualResetEdgeTrigger](#1-问题定义什么是-manualreset触发)
2. [协议时序与数据流](#2-协议时序与数据流)
3. [核心数据结构设计](#3-核心数据结构设计)
4. [TDD 第一步：IClock 时间注入接口](#4-tdd-第一步iclock-时间注入接口)
5. [TDD 第二步：PendingEdge 队列管理](#5-tdd-第二步pendingedge-队列管理)
6. [TDD 第三步：sendEdgeTrigger — 写入 ON 并入队](#6-tdd-第三步sendedgetrigger--写入-on-并入队)
7. [TDD 第四步：servicePendingEdgeTriggers — 扫描并写入 OFF](#7-tdd-第四步servicependingedgetriggers--扫描并写入-off)
8. [TDD 第五步：pollFeedback 中的集成调用](#8-tdd-第五步pollfeedback-中的集成调用)
9. [TDD 第六步：边界条件与异常路径](#9-tdd-第六步边界条件与异常路径)
10. [集成到 ModbusSystemDriver 的完整实现](#10-集成到-modbussystemdriver-的完整实现)
11. [构建与运行命令](#11-构建与运行命令)
12. [附录](#附录)

---

## 1. 问题定义：什么是 ManualResetEdgeTrigger

### 1.1 寄存器行为枚举

在 `infrastructure/plc/protocol/RegisterMetadata.h` 中，定义了 5 种寄存器行为语义：

```cpp
enum class RegisterBehavior {
    Level,                    // 电平触发 (持续保持) — 如 Enable、Jog ON/OFF
    ManualResetEdgeTrigger,   // 手动复位边沿触发 (软件需控制 ON → delay → OFF)
    AutoResetEdgeTrigger,     // 自动复位边沿触发 (PLC 端自动复位，软件只需发 ON)
    Continuous,               // 连续状态反馈 (只读)
    Latch                     // 锁存状态 (需明确 Reset)
};
```

**ManualResetEdgeTrigger** 的含义是：

> PLC 端的该寄存器是**边沿触发型**（上升沿有效），但 PLC **不会自动复位**该寄存器。  
> 软件必须在写入 `true` 后，等待指定脉冲宽度（`pulseWidthMs`），再写入 `false`。  
> 如果只写 `true` 不写 `false`，该寄存器将一直保持为 `ON`，导致 PLC 无法响应后续边沿。

### 1.2 受影响的命令（v4.0）

根据设计文档 §3.2，以下 5 个命令的寄存器需要使用 ManualResetEdgeTrigger：

| 命令 | X 轴 (龙门) | Y 轴 | Z 轴 | R 轴 | 脉冲宽度 |
|------|------------|------|------|------|---------|
| **ZeroAbsoluteCommand** | M30 (CLEAR_ABS_POS) | M31 | M32 | M33 | 150ms |
| **SetRelativeZeroCommand** | M14 (SET_REL_ZERO) | M15 | M16 | M17 | 150ms |
| **ClearRelativeZeroCommand** | M18 (CLEAR_REL_ZERO) | M19 | M20 | M21 | 150ms |
| **TriggerAbsMoveCommand** | M40 (ABS_MOVE_TRIGGER) | M42 | M44 | M46 | 150ms |
| **TriggerRelMoveCommand** | M41 (REL_MOVE_TRIGGER) | M43 | M45 | M47 | 150ms |

### 1.3 脉冲宽度来源

`RegisterInfo` 中已定义 `pulseWidthMs` 字段：

```cpp
struct RegisterInfo {
    // ... 其他字段 ...
    uint32_t pulseWidthMs;   // 脉冲宽度 (仅对 ManualResetEdgeTrigger 有效)
    // ...
};
```

驱动层默认使用 `EDGE_TRIGGER_PULSE_MS = 150` 作为全局常量，同时保留 `RegisterInfo::pulseWidthMs` 以备未来不同寄存器使用不同脉宽。

---

## 2. 协议时序与数据流

### 2.1 理想时序

```
时间轴 ──────────────────────────────────────────────────────►

T0:                  T0+150ms:
writeBool(reg, true)  writeBool(reg, false)
│                     │
├──── ON 脉冲 ────────┤
│     (150ms)         │
▼                     ▼
PLC 检测上升沿        PLC 检测下降沿
触发对应操作          寄存器归零，准备下一次触发
```

### 2.2 驱动层数据流

```
send(SystemCommand)
  └─ sendAxisCommand()
       └─ sendEdgeTrigger(reg)
            ├─ m_device.writeBool(reg, true)     // 1. 立即发送 ON
            └─ m_pendingEdges.push_back({reg, now, false})  // 2. 入队列

┌─────────────── 主循环 tick ───────────────┐
│                                            │
│  pollFeedback(ctx)                         │
│    ├─ servicePendingEdgeTriggers()         │  // 3. 扫描队列
│    │    ├─ 检查 elapsed >= 150ms?          │
│    │    └─ m_device.writeBool(reg, false)  │  // 4. 发送 OFF
│    └─ ... 反馈轮询 ...                     │
└────────────────────────────────────────────┘
```

### 2.3 为什么在 pollFeedback 中调度而不是独立线程

- **非阻塞**：Modbus 通讯是同步的，不应为脉冲管理开独立线程。
- **自然频率**：`pollFeedback` 在主循环 tick 中被调用（通常 10~50ms 一次），150ms 的精度完全满足。
- **简化状态**：消除多线程同步问题。`PendingEdge` 队列仅在主循环线程中访问。

---

## 3. 核心数据结构设计

### 3.1 PendingEdge 结构体

```cpp
struct PendingEdge {
    const protocol::RegisterInfo& reg;  // 目标寄存器引用（不可为空）
    std::chrono::steady_clock::time_point onTime;  // ON 写入时刻
    bool offSent = false;               // OFF 是否已发送（标记清理用）
};
```

**字段说明**：

| 字段 | 类型 | 用途 |
|------|------|------|
| `reg` | `const RegisterInfo&` | 引用已注册的寄存器信息，包含 `pulseWidthMs` |
| `onTime` | `time_point` | 记录 `writeBool(true)` 的时刻，用于计算已过时间 |
| `offSent` | `bool` | 标记 OFF 脉冲是否已发送。已发送的条目在循环末尾统一移除 |

**为什么不使用 `unique_ptr` / 拷贝**：
- `RegisterInfo` 是 `constexpr` 全局常量，生命周期等同于程序全周期，引用安全。
- 避免不必要的拷贝（`RegisterInfo` 约 40+ 字节，包含 `endianOverride` 等字段）。

### 3.2 容器选择：`std::vector<PendingEdge>` 还是 `std::list`？

选择 **`std::vector`**，理由：

1. **并发边沿数量有限**：同一时刻最多 5 个触发（5 种命令 × 1 个轴），通常更少。线性扫描开销可忽略。
2. **缓存友好**：`vector` 连续内存，比 `list` 更适合少量元素。
3. **删除模式**：使用 erase-remove idiom 批量删除已完成的条目，O(n) 但 n 很小。
4. **简单性**：无需自定义 allocator，`vector` 是 C++ 最常用容器。

### 3.3 ModbusSystemDriver 中的成员

```cpp
class ModbusSystemDriver : public ISystemDriver {
private:
    static constexpr int EDGE_TRIGGER_PULSE_MS = 150;  // 默认脉冲宽度
    std::vector<PendingEdge> m_pendingEdges;            // 挂起的边沿触发队列
    std::unique_ptr<IClock> m_clock;                    // 时间注入（可替换为 FakeClock）
};
```

---

## 4. TDD 第一步：IClock 时间注入接口

> **TDD 原则**：先写测试，再写实现。本阶段所有测试在 `test_modbus_system_driver_edge_trigger.cpp` 中。

### 4.1 为什么需要 IClock 接口

直接调用 `std::chrono::steady_clock::now()` 会导致测试**无法控制时间推进**。  
150ms 的等待在单元测试中不可接受。因此引入 `IClock` 抽象，允许测试注入 `FakeClock`。

### 4.2 RED — 测试驱动接口设计

#### 测试文件：`tests/infrastructure/test_modbus_system_driver_edge_trigger.cpp`

```cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>

// =============================================================================
// 测试套件：IClock 接口
// =============================================================================

// 测试 SteadyClock::now() 返回真实时间
TEST(IClockTest, SteadyClockReturnsRealTime) {
    SteadyClock clock;
    auto t1 = clock.now();
    // 无法直接断言时间值，但可以验证单调性
    auto t2 = clock.now();
    EXPECT_GE(t2, t1);
}

// 测试 FakeClock::now() 返回固定时间
TEST(IClockTest, FakeClockReturnsControlledTime) {
    FakeClock clock;
    auto t0 = clock.now();
    EXPECT_EQ(t0.time_since_epoch().count(), 0);  // 初始时刻为 epoch

    clock.advance(std::chrono::milliseconds(100));
    auto t1 = clock.now();
    EXPECT_EQ(
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
        100
    );
}

// 测试多次 advance 的累加效果
TEST(IClockTest, FakeClockAccumulatesTime) {
    FakeClock clock;
    clock.advance(std::chrono::milliseconds(50));
    clock.advance(std::chrono::milliseconds(50));
    clock.advance(std::chrono::milliseconds(50));

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock.now().time_since_epoch()
    ).count();
    EXPECT_EQ(elapsed, 150);
}
```

### 4.3 GREEN — 实现 IClock / SteadyClock / FakeClock

#### 文件：`infrastructure/utils/IClock.h`

```cpp
// infrastructure/utils/IClock.h
#pragma once

#include <chrono>

/**
 * @brief 时钟抽象接口，用于时间注入与测试可控
 *
 * 生产环境使用 SteadyClock，测试环境使用 FakeClock。
 * 主要用于 EdgeTrigger 脉冲管理中计算 elapsed >= pulseWidthMs。
 */
class IClock {
public:
    virtual ~IClock() = default;
    virtual std::chrono::steady_clock::time_point now() const = 0;
};

/**
 * @brief 生产环境时钟：直接委托 std::chrono::steady_clock::now()
 */
class SteadyClock : public IClock {
public:
    std::chrono::steady_clock::time_point now() const override {
        return std::chrono::steady_clock::now();
    }
};

/**
 * @brief 测试环境时钟：时间完全可控
 *
 * 初始时刻为 epoch (0)。
 * 每次调用 advance() 累加时间偏移。
 */
class FakeClock : public IClock {
public:
    std::chrono::steady_clock::time_point now() const override {
        return m_now;
    }

    void advance(std::chrono::milliseconds ms) {
        m_now += ms;
    }

private:
    std::chrono::steady_clock::time_point m_now{};
};
```

### 4.4 REFACTOR

- 确认 `SteadyClock` 和 `FakeClock` 都是 **header-only**，无需 `.cpp` 文件。
- `FakeClock` 放在 `infrastructure/utils/IClock.h` 中而非测试目录，因为它可能被其他测试复用。

---

## 5. TDD 第二步：PendingEdge 队列管理

### 5.1 目标

验证 `PendingEdge` 结构体的创建、入队、时间戳记录，以及队列的基本操作（获取挂起数量、按时间过滤）。

### 5.2 RED — 测试驱动

```cpp
// =============================================================================
// 测试套件：PendingEdge 队列管理
// =============================================================================

class ModbusSystemDriverEdgeTriggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockDevice_ = std::make_unique<StrictMock<MockPlcDevice>>();
        fakeClock_ = std::make_unique<FakeClock>();

        driver_ = std::make_unique<ModbusSystemDriver>(
            plc::protocol::INOVANCE_PROFILE
        );
        driver_->setDevice(mockDevice_.get());
        driver_->setClock(std::move(fakeClock_));  // 注入 FakeClock
    }

    // 辅助：快速推进时间
    void advanceTime(int ms) {
        // FakeClock 已被移动进 driver_，需要通过 driver_ 暴露接口
        driver_->advanceTime(std::chrono::milliseconds(ms));
    }

    std::unique_ptr<StrictMock<MockPlcDevice>> mockDevice_;
    std::unique_ptr<FakeClock> fakeClock_;  // 原始指针在 SetUp 中移动
    std::unique_ptr<ModbusSystemDriver> driver_;
};

// 初始状态：队列为空
TEST_F(ModbusSystemDriverEdgeTriggerTest, PendingQueueInitiallyEmpty) {
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}

// 写入 ON 后队列应有 1 个待处理项
TEST_F(ModbusSystemDriverEdgeTriggerTest, OnPulseIncrementsQueueCount) {
    const auto& reg = plc::reg::x_axis::command::CLEAR_ABS_POS;  // M30

    EXPECT_CALL(*mockDevice_, writeBool(reg, true))
        .WillOnce(Return(CommunicationResult::Sent()));

    auto result = driver_->sendEdgeTrigger(reg);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}

// 写入 ON 后，时间未到 150ms，不应被清除
TEST_F(ModbusSystemDriverEdgeTriggerTest, QueuePersistsWhenPulseNotExpired) {
    const auto& reg = plc::reg::x_axis::command::ABS_MOVE_TRIGGER;  // M40

    EXPECT_CALL(*mockDevice_, writeBool(reg, true))
        .WillOnce(Return(CommunicationResult::Sent()));

    driver_->sendEdgeTrigger(reg);
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);

    // 仅推进 100ms -> 脉冲未到期
    driver_->advanceTime(std::chrono::milliseconds(100));
    driver_->servicePendingEdgeTriggers();

    // 不应触发 writeBool(false)，队列仍保留
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}
```

### 5.3 GREEN — 实现队列管理

在 `ModbusSystemDriver` 中添加：

```cpp
// 头文件中新增成员声明
private:
    std::vector<PendingEdge> m_pendingEdges;
    std::unique_ptr<IClock> m_clock;

public:
    // 暴露给测试的接口
    void setClock(std::unique_ptr<IClock> clock) { m_clock = std::move(clock); }
    void advanceTime(std::chrono::milliseconds ms);  // 仅测试用
    size_t pendingEdgeCount() const { return m_pendingEdges.size(); }
```

`setClock` 允许测试注入 `FakeClock`。生产环境在构造函数中默认注入 `SteadyClock`。

---

## 6. TDD 第三步：sendEdgeTrigger — 写入 ON 并入队

### 6.1 目标

实现 `sendEdgeTrigger(const RegisterInfo& reg)` 的完整逻辑：
1. 调用 `m_device.writeBool(reg, true)` 写入 ON
2. 如果写入成功，将 `{reg, now(), false}` 加入 `m_pendingEdges` 队列
3. 如果写入失败，返回失败结果，**不入队**（避免泄漏）

### 6.2 RED — 测试驱动

```cpp
// =============================================================================
// 测试套件：sendEdgeTrigger 写入 ON 与入队
// =============================================================================

// 正常场景：ON 写入成功 -> 入队
TEST_F(ModbusSystemDriverEdgeTriggerTest, SendEdgeTriggerSuccessEnqueues) {
    const auto& reg = plc::reg::x_axis::command::CLEAR_ABS_POS;  // M30

    // 期望 writeBool(reg, true) 被调用一次
    EXPECT_CALL(*mockDevice_, writeBool(reg, true))
        .WillOnce(Return(CommunicationResult::Sent()));

    auto result = driver_->sendEdgeTrigger(reg);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}

// 异常场景：ON 写入失败 -> 不入队
TEST_F(ModbusSystemDriverEdgeTriggerTest, SendEdgeTriggerFailureDoesNotEnqueue) {
    const auto& reg = plc::reg::x_axis::command::ABS_MOVE_TRIGGER;  // M40

    // 模拟网络错误
    EXPECT_CALL(*mockDevice_, writeBool(reg, true))
        .WillOnce(Return(CommunicationResult::NetworkError()));

    auto result = driver_->sendEdgeTrigger(reg);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);

    // 确保 writeBool(reg, false) 永远不会被调用（因为没有入队）
    // StrictMock 会自动验证：没有额外调用
}

// 两个不同寄存器分别触发，队列应包含 2 个条目
TEST_F(ModbusSystemDriverEdgeTriggerTest, TwoDifferentRegistersEnqueueSeparately) {
    const auto& regA = plc::reg::x_axis::command::SET_REL_ZERO;     // M14
    const auto& regB = plc::reg::x_axis::command::CLEAR_REL_ZERO;   // M18

    EXPECT_CALL(*mockDevice_, writeBool(regA, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(regB, true))
        .WillOnce(Return(CommunicationResult::Sent()));

    driver_->sendEdgeTrigger(regA);
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);

    driver_->sendEdgeTrigger(regB);
    EXPECT_EQ(driver_->pendingEdgeCount(), 2);
}
```

### 6.3 GREEN — 实现 sendEdgeTrigger

```cpp
CommunicationResult ModbusSystemDriver::sendEdgeTrigger(
    const protocol::RegisterInfo& reg
) {
    // 1. 立即发送 ON 脉冲
    auto result = m_device.writeBool(reg, true);
    if (!result.isOk()) {
        return result;  // 写入失败，不入队
    }

    // 2. 记录时间戳并入队
    m_pendingEdges.push_back(PendingEdge{
        reg,
        m_clock->now(),  // 使用注入的时钟
        false            // offSent = false
    });

    return result;
}
```

### 6.4 REFACTOR

- 确认在 `sendAxisCommand()` 中，以下分支使用 `sendEdgeTrigger`：
  - `ZeroAbsoluteCommand` -> `regCmdClearAbsPos(id)`
  - `SetRelativeZeroCommand` -> `regCmdSetRelZero(id)`
  - `ClearRelativeZeroCommand` -> `regCmdClearRelZero(id)`
  - `TriggerAbsMoveCommand` -> `regCmdAbsTrigger(id)`
  - `TriggerRelMoveCommand` -> `regCmdRelTrigger(id)`

- 确认 `EmergencyStopCommand` **不使用** `sendEdgeTrigger`（Level 型，直接 `writeBool`）。

---

## 7. TDD 第四步：servicePendingEdgeTriggers — 扫描并写入 OFF

### 7.1 目标

实现 `servicePendingEdgeTriggers()` 方法：

1. 遍历 `m_pendingEdges` 队列
2. 对每个 `offSent == false` 的条目，计算 `elapsed = now - onTime`
3. 如果 `elapsed >= pulseWidthMs`（默认 150ms），调用 `m_device.writeBool(reg, false)`
4. 标记 `offSent = true`
5. 遍历完成后，使用 erase-remove idiom 移除所有 `offSent == true` 的条目

### 7.2 关键设计决策

**选择 `pulseWidthMs` 来源**：优先使用 `RegisterInfo::pulseWidthMs`。如果为 0，fallback 到全局默认 `EDGE_TRIGGER_PULSE_MS = 150`。

```
effectivePulseWidth = reg.pulseWidthMs > 0 ? reg.pulseWidthMs : EDGE_TRIGGER_PULSE_MS
```

这样设计的好处：
- 当前所有寄存器使用统一的 150ms，简单清晰。
- 未来如果有寄存器需要不同的脉宽（如 50ms 或 200ms），只需在 `RegisterAddressAll.h` 中设置 `pulseWidthMs`，无需改代码。

### 7.3 RED — 测试驱动

```cpp
// =============================================================================
// 测试套件：servicePendingEdgeTriggers — 时间触发 OFF
// =============================================================================

// 单次 EdgeTrigger 完整生命周期：ON -> 150ms -> OFF
TEST_F(ModbusSystemDriverEdgeTriggerTest, SingleEdgeTriggerFullLifecycle) {
    const auto& reg = plc::reg::x_axis::command::CLEAR_ABS_POS;  // M30

    // Step 1: 预期 ON
    EXPECT_CALL(*mockDevice_, writeBool(reg, true))
        .WillOnce(Return(CommunicationResult::Sent()));

    auto result = driver_->sendEdgeTrigger(reg);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);

    // Step 2: 时间未到 150ms，servicePendingEdgeTriggers 不应发送 OFF
    driver_->advanceTime(std::chrono::milliseconds(100));
    driver_->servicePendingEdgeTriggers();
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);  // 仍在队列中

    // Step 3: 推进到 150ms，预期 OFF
    EXPECT_CALL(*mockDevice_, writeBool(reg, false))
        .WillOnce(Return(CommunicationResult::Sent()));

    driver_->advanceTime(std::chrono::milliseconds(50));  // 累计 150ms
    driver_->servicePendingEdgeTriggers();

    EXPECT_EQ(driver_->pendingEdgeCount(), 0);  // 队列已清空
}

// 多个并发 EdgeTrigger 独立计时
TEST_F(ModbusSystemDriverEdgeTriggerTest, MultipleConcurrentEdgeTriggersIndependentTiming) {
    const auto& regA = plc::reg::x_axis::command::CLEAR_ABS_POS;   // M30
    const auto& regB = plc::reg::y_axis::command::CLEAR_ABS_POS;   // M31

    // 两个寄存器分别写入 ON
    EXPECT_CALL(*mockDevice_, writeBool(regA, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(regB, true))
        .WillOnce(Return(CommunicationResult::Sent()));

    driver_->sendEdgeTrigger(regA);  // T=0
    driver_->advanceTime(std::chrono::milliseconds(10));
    driver_->sendEdgeTrigger(regB);  // T=10
    EXPECT_EQ(driver_->pendingEdgeCount(), 2);

    // T=150: regA 到期，regB 差 10ms
    driver_->advanceTime(std::chrono::milliseconds(140));  // 累计 T=150

    EXPECT_CALL(*mockDevice_, writeBool(regA, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    // regB 不应此时触发

    driver_->servicePendingEdgeTriggers();
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);  // 只剩 regB

    // T=160: regB 也到期
    driver_->advanceTime(std::chrono::milliseconds(10));   // 累计 T=160

    EXPECT_CALL(*mockDevice_, writeBool(regB, false))
        .WillOnce(Return(CommunicationResult::Sent()));

    driver_->servicePendingEdgeTriggers();
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);  // 全部清空
}

// 同一寄存器连续两次触发（覆盖前一个）
TEST_F(ModbusSystemDriverEdgeTriggerTest, SameRegisterTriggeredTwiceSequentially) {
    const auto& reg = plc::reg::x_axis::command::REL_MOVE_TRIGGER;  // M41

    // 第一次触发：T=0
    EXPECT_CALL(*mockDevice_, writeBool(reg, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    driver_->sendEdgeTrigger(reg);
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);

    // T=50: 第一次还没到期
    driver_->advanceTime(std::chrono::milliseconds(50));

    // 第二次触发：T=50
    EXPECT_CALL(*mockDevice_, writeBool(reg, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    driver_->sendEdgeTrigger(reg);
    EXPECT_EQ(driver_->pendingEdgeCount(), 2);  // 两个独立的 PendingEdge

    // T=200: 第一个到期（T=0 + 150 = 150 <= 200）
    EXPECT_CALL(*mockDevice_, writeBool(reg, false))
        .WillOnce(Return(CommunicationResult::Sent()));

    driver_->advanceTime(std::chrono::milliseconds(150));  // 累计 T=200
    driver_->servicePendingEdgeTriggers();
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);  // 第二个还在

    // T=250: 第二个也到期（T=50 + 150 = 200 <= 250）
    EXPECT_CALL(*mockDevice_, writeBool(reg, false))
        .WillOnce(Return(CommunicationResult::Sent()));

    driver_->advanceTime(std::chrono::milliseconds(50));   // 累计 T=250
    driver_->servicePendingEdgeTriggers();
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

### 7.4 GREEN — 实现 servicePendingEdgeTriggers

```cpp
void ModbusSystemDriver::servicePendingEdgeTriggers() {
    auto now = m_clock->now();

    // 第一步：扫描并发送到期的 OFF 脉冲
    for (auto& edge : m_pendingEdges) {
        if (edge.offSent) {
            continue;  // 已发送 OFF，跳过
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - edge.onTime
        ).count();

        // 使用 RegisterInfo::pulseWidthMs，若为 0 则 fallback 到默认值
        uint32_t pulseWidth = edge.reg.pulseWidthMs > 0
                                ? edge.reg.pulseWidthMs
                                : EDGE_TRIGGER_PULSE_MS;

        if (elapsed >= static_cast<long long>(pulseWidth)) {
            m_device.writeBool(edge.reg, false);
            edge.offSent = true;
        }
    }

    // 第二步：清理已完成的条目（erase-remove idiom）
    m_pendingEdges.erase(
        std::remove_if(
            m_pendingEdges.begin(),
            m_pendingEdges.end(),
            [](const PendingEdge& e) { return e.offSent; }
        ),
        m_pendingEdges.end()
    );
}
```

### 7.5 设计要点分析

**为什么使用两步法（先标记 offSent，再统一移除）？**

```cpp
// 错误做法：在遍历中直接删除
for (auto it = m_pendingEdges.begin(); it != m_pendingEdges.end(); ) {
    if (elapsed >= pulseWidth) {
        m_device.writeBool(it->reg, false);
        it = m_pendingEdges.erase(it);  // 迭代器失效风险 + 不必要的拷贝
    } else {
        ++it;
    }
}

// 正确做法：标记 + 批量移除
for (auto& edge : m_pendingEdges) {
    if (elapsed >= pulseWidth) {
        m_device.writeBool(edge.reg, false);
        edge.offSent = true;
    }
}
m_pendingEdges.erase(
    std::remove_if(..., [](const PendingEdge& e) { return e.offSent; }),
    m_pendingEdges.end()
);
```

**两步法的优势**：
1. **代码清晰**：写入逻辑和清理逻辑分离。
2. **性能**：`erase-remove` idiom 是标准库推荐的高效模式，O(n) 但只移动一次。
3. **安全**：遍历时不会因为 `erase` 导致迭代器失效。

### 7.6 REFACTOR

- 确认 `servicePendingEdgeTriggers()` 在 `pollFeedback()` 中**最先调用**（确保先发送 OFF，再读取反馈）。
- 确认 `offSent` 标记在 OFF 写入失败时**仍然设为 true**（防止队列泄漏，见 §9）。

---

## 8. TDD 第五步：pollFeedback 中的集成调用

### 8.1 目标

验证 `pollFeedback()` 中 `servicePendingEdgeTriggers()` 的正确调用顺序：
1. 先 `servicePendingEdgeTriggers()` — 处理到期的 OFF 脉冲
2. 再 `m_poller.poll()` — 批量读取 PLC 反馈
3. 最后 `m_device.updateSnapshot()` + `readAxisFeedback()` + `readSystemFeedback()`

### 8.2 RED — 测试驱动

```cpp
// =============================================================================
// 测试套件：pollFeedback 中的 EdgeTrigger 集成
// =============================================================================

// pollFeedback 应在读取反馈前先处理到期的 EdgeTrigger
TEST_F(ModbusSystemDriverEdgeTriggerTest,
       PollFeedbackServicesEdgeTriggersBeforeReading) {
    const auto& reg = plc::reg::x_axis::command::CLEAR_ABS_POS;  // M30

    // Step 1: 发送 ON 脉冲
    EXPECT_CALL(*mockDevice_, writeBool(reg, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    driver_->sendEdgeTrigger(reg);

    // Step 2: 推进 150ms
    driver_->advanceTime(std::chrono::milliseconds(150));

    // Step 3: 预期在 pollFeedback 中：
    //   首先调用 writeBool(reg, false)
    //   然后再进行反馈读取

    // 使用 InSequence 确保调用顺序
    {
        testing::InSequence seq;

        // 先发送 OFF
        EXPECT_CALL(*mockDevice_, writeBool(reg, false))
            .WillOnce(Return(CommunicationResult::Sent()));

        // 再调用反馈读取（mock 中的各种 readXxx）
        EXPECT_CALL(*mockDevice_, isStateTrusted())
            .WillOnce(Return(true));
        // ... 其他 readXxx 预期 ...
    }

    SystemContext ctx;
    driver_->pollFeedback(ctx);

    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}

// 即使没有到期的 EdgeTrigger，pollFeedback 也应正常执行
TEST_F(ModbusSystemDriverEdgeTriggerTest,
       PollFeedbackWorksWhenNoPendingEdges) {
    EXPECT_CALL(*mockDevice_, isStateTrusted())
        .WillOnce(Return(true));
    // ... 其他 readXxx 预期 ...

    SystemContext ctx;
    driver_->pollFeedback(ctx);  // 不应崩溃或挂起
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

### 8.3 GREEN — 实现 pollFeedback 中的调度

```cpp
void ModbusSystemDriver::pollFeedback(SystemContext& ctx) {
    // =============================================
    // 第一步：处理到期的 EdgeTrigger OFF 脉冲
    // =============================================
    servicePendingEdgeTriggers();

    // =============================================
    // 第二步：批量轮询 PLC
    // =============================================
    PollRequest req = m_poller.buildRequest();
    PlcSnapshot snap = m_poller.poll(*m_client, req);
    m_device.updateSnapshot(std::move(snap));

    // =============================================
    // 第三步：状态信任检查
    // =============================================
    if (!m_device.isStateTrusted()) {
        return;  // 数据不可信，不向 Domain 层注入
    }

    // =============================================
    // 第四步：分发轴反馈
    // =============================================
    for (auto axId : {AxisId::X, AxisId::Y, AxisId::Z, AxisId::R}) {
        readAxisFeedback(axId, ctx);
    }

    // =============================================
    // 第五步：分发系统反馈
    // =============================================
    readSystemFeedback(ctx);
}
```

**调用顺序的设计理由**：

```
servicePendingEdgeTriggers()  <- 先发送 OFF，清空过期脉冲
       |
       v
m_poller.poll()               <- 再读取反馈，确保 PLC 已处理 OFF 后的状态
       |
       v
updateSnapshot() + readXxx()  <- 最后更新快照并分发
```

如果先读取反馈再发送 OFF，会导致：
- 当次 poll 读取到的寄存器值仍然是 ON 状态（因为 OFF 还没发）
- 需要再等一个周期才能读到 OFF 后的正确值

### 8.4 时序精度分析

| 场景 | pollFeedback 调用间隔 | EdgeTrigger OFF 延迟 | 实际脉冲宽度 |
|------|----------------------|---------------------|------------|
| 正常 (10~20ms) | 15ms (平均) | 最多 1 个间隔 = 15~20ms | 150ms + 0~20ms |
| 慢速 (50ms) | 50ms | 最多 1 个间隔 = 50ms | 150ms + 0~50ms |
| 快速 (5ms) | 5ms | 最多 1 个间隔 = 5ms | 150ms + 0~5ms |

**结论**：**周期误差最大为 1 个 pollFeedback 间隔**，在 10~50ms 的典型范围内，150ms ± 50ms 对 PLC 来说完全可接受。无需引入独立定时器线程。

---

## 9. TDD 第六步：边界条件与异常路径

### 9.1 目标

覆盖以下边界场景：

1. **OFF 写入失败**：如果 `writeBool(reg, false)` 失败，如何处理？不应无限重试，也不应泄漏队列。
2. **设备未连接**：当 `m_device` 为 nullptr 或设备未初始化时，`sendEdgeTrigger` 和 `servicePendingEdgeTriggers` 不能崩溃。
3. **相同寄存器同时多次触发**：同一寄存器在短时间内被发送多次 ON，OFF 应逐个发送。
4. **时钟回退**：`steady_clock` 不会回退，但如果 `FakeClock` 被意外重置，需要处理。
5. **列队清空**：析构时如果有未完成的 `PendingEdge`，应该如何处理？

### 9.2 RED — 测试驱动

```cpp
// =============================================================================
// 测试套件：边界条件与异常路径
// =============================================================================

// 边界 1: OFF 写入失败 → 仍然标记 offSent = true（防止泄漏）
TEST_F(ModbusSystemDriverEdgeTriggerTest, OffWriteFailureStillMarksOffSent) {
    const auto& reg = plc::reg::x_axis::command::CLEAR_ABS_POS;  // M30

    // 写入 ON 成功
    EXPECT_CALL(*mockDevice_, writeBool(reg, true))
        .WillOnce(Return(CommunicationResult::Sent()));

    driver_->sendEdgeTrigger(reg);
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);

    // 推进 150ms
    driver_->advanceTime(std::chrono::milliseconds(150));

    // 写入 OFF 失败
    EXPECT_CALL(*mockDevice_, writeBool(reg, false))
        .WillOnce(Return(CommunicationResult::NetworkError()));

    // servicePendingEdgeTriggers 不应抛异常
    driver_->servicePendingEdgeTriggers();

    // 关键：即使 OFF 写入失败，队列也应清空（不泄漏）
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}

// 边界 2: 设备未初始化时 sendEdgeTrigger 不应崩溃
TEST_F(ModbusSystemDriverEdgeTriggerTest, SendEdgeTriggerWithoutDeviceDoesNotCrash) {
    // 构造一个没有 setDevice 的 driver_
    auto driver_no_device = std::make_unique<ModbusSystemDriver>(
        plc::protocol::INOVANCE_PROFILE
    );
    driver_no_device->setClock(std::make_unique<FakeClock>());

    const auto& reg = plc::reg::x_axis::command::CLEAR_ABS_POS;

    // 不应崩溃
    auto result = driver_no_device->sendEdgeTrigger(reg);
    EXPECT_FALSE(result.ok());  // 应返回设备未初始化的错误
    EXPECT_EQ(driver_no_device->pendingEdgeCount(), 0);
}

// 边界 3: 服务空队列不应崩溃
TEST_F(ModbusSystemDriverEdgeTriggerTest, ServiceEmptyQueueDoesNotCrash) {
    // 直接调用 servicePendingEdgeTriggers 而不添加任何边沿
    driver_->servicePendingEdgeTriggers();
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);  // 无害操作
}

// 边界 4: 析构时清空挂起的边沿
TEST_F(ModbusSystemDriverEdgeTriggerTest, DestructorClearsPendingEdges) {
    const auto& reg = plc::reg::x_axis::command::CLEAR_ABS_POS;

    // 写入 ON 成功，但未推进时间（OFF 未发送）
    EXPECT_CALL(*mockDevice_, writeBool(reg, true))
        .WillOnce(Return(CommunicationResult::Sent()));

    driver_->sendEdgeTrigger(reg);
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);

    // 析构 driver_ → 不应尝试写入 OFF（设备可能已释放）
    // StrictMock 会验证：析构期间没有额外调用 writeBool
    driver_.reset();

    // 注：如果析构时需要写入 OFF，需在 SetUp 中另作安排
}

// 边界 5: 相同寄存器同时多次触发（已在 §7.3 测试）
// 边界 6: 自定义 pulseWidthMs 生效
TEST_F(ModbusSystemDriverEdgeTriggerTest, CustomPulseWidthIsRespected) {
    // 使用 pulseWidthMs = 50ms 的寄存器
    const auto& reg = plc::reg::x_axis::command::SET_REL_ZERO;  // M14, pulseWidthMs = 150

    // 此测试需要构造一个自定义 pulseWidthMs=50ms 的 RegisterInfo
    // 由于 reg 是 constexpr 全局常量，此处使用默认 150ms
    // 实际测试中可通过 Mock 或临时构造 RegisterInfo 验证

    // 注：完整测试见 test_register_info_boundary.cpp
    (void)reg;  // 占位，实际实施时需补充
}
```

### 9.3 GREEN — 边界条件处理实现

```cpp
// =============================================
// sendEdgeTrigger: 防御设备未初始化
// =============================================
CommunicationResult ModbusSystemDriver::sendEdgeTrigger(
    const protocol::RegisterInfo& reg
) {
    if (!m_device) {
        return CommunicationResult::DeviceNotInitialized();
    }

    auto result = m_device.writeBool(reg, true);
    if (!result.isOk()) {
        return result;
    }

    m_pendingEdges.push_back(PendingEdge{
        reg,
        m_clock->now(),
        false
    });

    return result;
}

// =============================================
// servicePendingEdgeTriggers: OFF 写入失败仍标记 offSent
// =============================================
void ModbusSystemDriver::servicePendingEdgeTriggers() {
    auto now = m_clock->now();

    for (auto& edge : m_pendingEdges) {
        if (edge.offSent) continue;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - edge.onTime
        ).count();

        uint32_t pulseWidth = edge.reg.pulseWidthMs > 0
                                ? edge.reg.pulseWidthMs
                                : EDGE_TRIGGER_PULSE_MS;

        if (elapsed >= static_cast<long long>(pulseWidth)) {
            // 即使写入失败，也标记 offSent = true
            // 防止无限重试导致队列泄漏
            m_device.writeBool(edge.reg, false);
            edge.offSent = true;
        }
    }

    // 清理已完成的条目
    m_pendingEdges.erase(
        std::remove_if(
            m_pendingEdges.begin(),
            m_pendingEdges.end(),
            [](const PendingEdge& e) { return e.offSent; }
        ),
        m_pendingEdges.end()
    );
}
```

### 9.4 析构行为说明

`ModbusSystemDriver` 的析构函数遵循默认行为：

```cpp
~ModbusSystemDriver() = default;
```

当驱动被销毁时：
1. `m_pendingEdges` 向量自动析构，清理所有 `PendingEdge` 条目。
2. **不会**尝试发送未完成的 OFF 脉冲，因为 Modbus 连接可能已断开。
3. 这是**安全的**：PLC 端的寄存器保持 ON 状态，但在下次上电/重启时会被 PLC 自身清零。或者如果驱动被重新创建并重新连接，`pollFeedback` 中的初始化逻辑可以从 PLC 读取实际寄存器状态。

**为什么不在析构中发送 OFF？**

- 析构时网络可能已不可用，尝试写入会导致阻塞或异常。
- PLC 侧对这类命令寄存器通常有上电默认为 0 的机制。
- 如果确实需要确保 OFF，应在应用层（`SystemManager::shutdown()`） 中显式清理。

### 9.5 REFACTOR

- 确认 `CommunicationResult::DeviceNotInitialized()` 已在 `CommunicationResult` 枚举中定义。
- 确认所有边界测试通过。

---

## 10. 集成到 ModbusSystemDriver 的完整实现

### 10.1 需要修改的文件清单

| 文件 | 修改类型 | 说明 |
|------|---------|------|
| `infrastructure/utils/IClock.h` | **新建** | 时间注入接口 |
| `infrastructure/plc/ModbusSystemDriver.h` | 修改 | 添加成员、方法声明 |
| `infrastructure/plc/ModbusSystemDriver.cpp` | 修改 | 实现 sendEdgeTrigger / servicePendingEdgeTriggers |
| `infrastructure/plc/protocol/RegisterAddressAll.h` | 修改 | 为 5 种命令寄存器补充 `pulseWidthMs = 150` |
| `tests/infrastructure/CMakeLists.txt` | 修改 | 添加 `test_modbus_system_driver_edge_trigger` |
| `tests/infrastructure/test_modbus_system_driver_edge_trigger.cpp` | **新建** | 所有边沿触发 TDD 测试 |

### 10.2 ModbusSystemDriver.h 增量变更

```cpp
// =============================================
// 新增 include
// =============================================
#include "infrastructure/utils/IClock.h"
#include "infrastructure/utils/CommunicationResult.h"

// =============================================
// 新增 struct 声明（在 ModbusSystemDriver 类之前）
// =============================================
struct PendingEdge {
    const protocol::RegisterInfo& reg;
    std::chrono::steady_clock::time_point onTime;
    bool offSent = false;
};

// =============================================
// 在 class ModbusSystemDriver 内部新增
// =============================================
class ModbusSystemDriver : public ISystemDriver {
public:
    // --- 现有接口 ---
    explicit ModbusSystemDriver(protocol::ProfileType profile);
    ~ModbusSystemDriver() override = default;

    CommunicationResult send(const domain::command::SystemCommand& cmd) override;
    void pollFeedback(domain::entity::SystemContext& ctx) override;
    // ...

    // --- 新增：测试与注入接口 ---
    void setClock(std::unique_ptr<IClock> clock);
    void advanceTime(std::chrono::milliseconds ms);   // 仅供测试
    size_t pendingEdgeCount() const;                   // 仅供测试

    // --- 新增：边沿触发协议接口 ---
    CommunicationResult sendEdgeTrigger(const protocol::RegisterInfo& reg);
    void servicePendingEdgeTriggers();

private:
    // --- 现有成员 ---
    // ...

    // --- 新增成员 ---
    static constexpr int EDGE_TRIGGER_PULSE_MS = 150;
    std::vector<PendingEdge> m_pendingEdges;
    std::unique_ptr<IClock> m_clock;
};
```

### 10.3 ModbusSystemDriver 构造函数变更

```cpp
ModbusSystemDriver::ModbusSystemDriver(protocol::ProfileType profile)
    : m_profile(profile)
    , m_clock(std::make_unique<SteadyClock>())  // ← 新增：默认 SteadyClock
{
    // ... 现有初始化代码 ...
}

void ModbusSystemDriver::setClock(std::unique_ptr<IClock> clock) {
    m_clock = std::move(clock);
}

void ModbusSystemDriver::advanceTime(std::chrono::milliseconds ms) {
    // 仅在使用 FakeClock 时有效
    auto* fake = dynamic_cast<FakeClock*>(m_clock.get());
    if (fake) {
        fake->advance(ms);
    }
}
```

### 10.4 sendAxisCommand 中的分支修改

```cpp
CommunicationResult ModbusSystemDriver::sendAxisCommand(
    AxisId axisId, const domain::command::SystemCommand& cmd
) {
    const auto& info = regForAxis(axisId);

    return std::visit(overloaded{
        // =============================================
        // Level 型：直接 writeBool（不变）
        // =============================================
        [&](const domain::command::EnableCommand&) {
            return m_device.writeBool(info.regCmdEnable, true);
        },
        [&](const domain::command::DisableCommand&) {
            return m_device.writeBool(info.regCmdEnable, false);
        },
        [&](const domain::command::JogOnCommand&) {
            return m_device.writeBool(info.regCmdJogOn, true);
        },
        [&](const domain::command::JogOffCommand&) {
            return m_device.writeBool(info.regCmdJogOn, false);
        },
        [&](const domain::command::EmergencyStopCommand&) {
            return m_device.writeBool(info.regCmdEmergencyStop, true);
        },
        [&](const domain::command::ReleaseEmergencyStopCommand&) {
            return m_device.writeBool(info.regCmdEmergencyStop, false);
        },

        // =============================================
        // ManualResetEdgeTrigger 型：使用 sendEdgeTrigger
        // =============================================
        [&](const domain::command::ZeroAbsoluteCommand&) {
            return sendEdgeTrigger(info.regCmdClearAbsPos);
        },
        [&](const domain::command::SetRelativeZeroCommand&) {
            return sendEdgeTrigger(info.regCmdSetRelZero);
        },
        [&](const domain::command::ClearRelativeZeroCommand&) {
            return sendEdgeTrigger(info.regCmdClearRelZero);
        },
        [&](const domain::command::TriggerAbsMoveCommand&) {
            return sendEdgeTrigger(info.regCmdAbsTrigger);
        },
        [&](const domain::command::TriggerRelMoveCommand&) {
            return sendEdgeTrigger(info.regCmdRelTrigger);
        },

        // =============================================
        // AutoResetEdgeTrigger 型：仅写 ON（PLC 端自动复位）
        // =============================================
        [&](const domain::command::StopAxisCommand&) {
            return m_device.writeBool(info.regCmdStop, true);
        },
        // ... 其他 AutoReset 命令 ...

    }, cmd.payload);
}
```

### 10.5 RegisterAddressAll.h 增量变更

```cpp
// =============================================
// 为 5 种命令寄存器补充 pulseWidthMs = 150
// =============================================

// X 轴
static constexpr RegisterInfo X_CLEAR_ABS_POS {
    .address = 30,
    // ... 现有字段 ...
    .behavior = RegisterBehavior::ManualResetEdgeTrigger,
    .pulseWidthMs = 150   // ← 新增
};

static constexpr RegisterInfo X_SET_REL_ZERO {
    .address = 14,
    // ...
    .behavior = RegisterBehavior::ManualResetEdgeTrigger,
    .pulseWidthMs = 150   // ← 新增
};

static constexpr RegisterInfo X_CLEAR_REL_ZERO {
    .address = 18,
    // ...
    .behavior = RegisterBehavior::ManualResetEdgeTrigger,
    .pulseWidthMs = 150   // ← 新增
};

static constexpr RegisterInfo X_ABS_MOVE_TRIGGER {
    .address = 40,
    // ...
    .behavior = RegisterBehavior::ManualResetEdgeTrigger,
    .pulseWidthMs = 150   // ← 新增
};

static constexpr RegisterInfo X_REL_MOVE_TRIGGER {
    .address = 41,
    // ...
    .behavior = RegisterBehavior::ManualResetEdgeTrigger,
    .pulseWidthMs = 150   // ← 新增
};

// Y、Z、R 轴同理（地址不同）
```

### 10.6 tests/CMakeLists.txt 增量

```cmake
# 新增边沿触发协议测试
add_executable(test_modbus_system_driver_edge_trigger
    tests/infrastructure/test_modbus_system_driver_edge_trigger.cpp
    infrastructure/plc/ModbusSystemDriver.cpp
)
target_link_libraries(test_modbus_system_driver_edge_trigger
    gtest
    gmock
    # ... 其他依赖 ...
)
add_test(NAME ModbusSystemDriverEdgeTriggerTest
         COMMAND test_modbus_system_driver_edge_trigger)
```

---

## 11. 构建与运行命令

### 11.1 构建

```bash
# 进入 build 目录
cd f:/project/servoV6/build

# 配置 CMake（仅首次或 CMakeLists 变更后需要）
cmake ..

# 构建所有目标
cmake --build . --config Debug

# 或仅构建测试目标
cmake --build . --config Debug --target test_modbus_system_driver_edge_trigger
```

### 11.2 运行测试

```bash
# 运行边沿触发协议测试
ctest -C Debug -R ModbusSystemDriverEdgeTriggerTest --output-on-failure

# 或直接运行可执行文件
./tests/infrastructure/test_modbus_system_driver_edge_trigger.exe
```

### 11.3 预期测试输出

```
[==========] Running 15 tests from 3 test suites.
[----------] Global test environment set-up.

[----------] 3 tests from IClockTest
[ RUN      ] IClockTest.SteadyClockReturnsRealTime
[       OK ] IClockTest.SteadyClockReturnsRealTime (0 ms)
[ RUN      ] IClockTest.FakeClockReturnsControlledTime
[       OK ] IClockTest.FakeClockReturnsControlledTime (0 ms)
[ RUN      ] IClockTest.FakeClockAccumulatesTime
[       OK ] IClockTest.FakeClockAccumulatesTime (0 ms)
[----------] 3 tests from IClockTest (0 ms total)

[----------] 10 tests from ModbusSystemDriverEdgeTriggerTest
[ RUN      ] ModbusSystemDriverEdgeTriggerTest.PendingQueueInitiallyEmpty
[       OK ] ModbusSystemDriverEdgeTriggerTest.PendingQueueInitiallyEmpty (0 ms)
[ RUN      ] ModbusSystemDriverEdgeTriggerTest.OnPulseIncrementsQueueCount
[       OK ] ModbusSystemDriverEdgeTriggerTest.OnPulseIncrementsQueueCount (0 ms)
[ RUN      ] ModbusSystemDriverEdgeTriggerTest.SendEdgeTriggerSuccessEnqueues
[       OK ] ModbusSystemDriverEdgeTriggerTest.SendEdgeTriggerSuccessEnqueues (0 ms)
[ RUN      ] ModbusSystemDriverEdgeTriggerTest.SendEdgeTriggerFailureDoesNotEnqueue
[       OK ] ModbusSystemDriverEdgeTriggerTest.SendEdgeTriggerFailureDoesNotEnqueue (0 ms)
[ RUN      ] ModbusSystemDriverEdgeTriggerTest.TwoDifferentRegistersEnqueueSeparately
[       OK ] ModbusSystemDriverEdgeTriggerTest.TwoDifferentRegistersEnqueueSeparately (0 ms)
[ RUN      ] ModbusSystemDriverEdgeTriggerTest.SingleEdgeTriggerFullLifecycle
[       OK ] ModbusSystemDriverEdgeTriggerTest.SingleEdgeTriggerFullLifecycle (0 ms)
[ RUN      ] ModbusSystemDriverEdgeTriggerTest.MultipleConcurrentEdgeTriggersIndependentTiming
[       OK ] ModbusSystemDriverEdgeTriggerTest.MultipleConcurrentEdgeTriggersIndependentTiming (0 ms)
[ RUN      ] ModbusSystemDriverEdgeTriggerTest.SameRegisterTriggeredTwiceSequentially
[       OK ] ModbusSystemDriverEdgeTriggerTest.SameRegisterTriggeredTwiceSequentially (0 ms)
[ RUN      ] ModbusSystemDriverEdgeTriggerTest.OffWriteFailureStillMarksOffSent
[       OK ] ModbusSystemDriverEdgeTriggerTest.OffWriteFailureStillMarksOffSent (0 ms)
[ RUN      ] ModbusSystemDriverEdgeTriggerTest.SendEdgeTriggerWithoutDeviceDoesNotCrash
[       OK ] ModbusSystemDriverEdgeTriggerTest.SendEdgeTriggerWithoutDeviceDoesNotCrash (0 ms)
[----------] 10 tests from ModbusSystemDriverEdgeTriggerTest (0 ms total)

[----------] 2 tests from PollFeedbackEdgeTriggerTest
[ RUN      ] PollFeedbackEdgeTriggerTest.PollFeedbackServicesEdgeTriggersBeforeReading
[       OK ] PollFeedbackEdgeTriggerTest.PollFeedbackServicesEdgeTriggersBeforeReading (0 ms)
[ RUN      ] PollFeedbackEdgeTriggerTest.PollFeedbackWorksWhenNoPendingEdges
[       OK ] PollFeedbackEdgeTriggerTest.PollFeedbackWorksWhenNoPendingEdges (0 ms)
[----------] 2 tests from PollFeedbackEdgeTriggerTest (0 ms total)

[----------] Global test environment tear-down
[==========] 15 tests from 3 test suites ran. (0 ms total)
[  PASSED  ] 15 tests.
```

---

## 12. 附录

### 附录 A：TDD 实现步骤总结

| 步骤 | RED（测试） | GREEN（实现） | REFACTOR | 预计耗时 |
|------|-----------|-------------|---------|---------|
| 1 | `IClockTest` 3 个测试 | `IClock.h` (header-only) | 确认 header-only | 0.5h |
| 2 | `PendingEdge` 队列测试 3 个 | `PendingEdge` 结构体 + 队列代码 | 确认容器选择 | 0.5h |
| 3 | `sendEdgeTrigger` 测试 3 个 | `sendEdgeTrigger` 实现 | 关联 `sendAxisCommand` | 1h |
| 4 | `servicePendingEdgeTriggers` 测试 4 个 | `servicePendingEdgeTriggers` 实现 | 两步法 erase-remove | 1.5h |
| 5 | `pollFeedback` 集成测试 2 个 | `pollFeedback` 中的调用顺序 | 确认时序精度 | 1h |
| 6 | 边界条件测试 5 个 | 防御性代码 | 析构行为确认 | 1h |
| **总计** | **20 个测试** | **3 个源文件修改** | | **约 5.5 小时** |

### 附录 B：关键设计决策记录

| 决策 | 选项 A | 选项 B | 选择 | 理由 |
|------|--------|--------|------|------|
| 时间调度方式 | 独立定时器线程 | pollFeedback 中同步调用 | **B** | 避免多线程同步，Modbus 是同步协议 |
| 容器选择 | `std::list` | `std::vector` | **B** | 少量元素，缓存友好，erase-remove idiom |
| 删除模式 | 遍历中 erase | 标记 + 批量 erase-remove | **B** | 代码清晰，迭代器安全 |
| 寄存器引用 | `unique_ptr<const T>` | `const T&` | **B** | `RegisterInfo` 是 constexpr 全局常量 |
| 脉冲宽度来源 | 全局常量 | `RegisterInfo::pulseWidthMs` | **两者结合** | 默认 150ms，可逐寄存器覆盖 |
| OFF 写入失败处理 | 重试 | 标记 offSent = true | **B** | 防止无限重试导致队列泄漏 |

### 附录 C：关键类型关系图

```
┌──────────────────────┐
│      IClock          │  抽象时钟接口
│  + now() : time_point│
└──────┬───────────────┘
       │
       ├──── SteadyClock    生产环境
       │     └─ now() → std::chrono::steady_clock::now()
       │
       └──── FakeClock      测试环境
             ├─ now() → m_now
             └─ advance(ms)

┌──────────────────────┐
│    PendingEdge       │  边沿触发待处理条目
│  + reg : RegisterInfo&│
│  + onTime : time_point│
│  + offSent : bool     │
└──────────────────────┘

┌─────────────────────────────────────┐
│    ModbusSystemDriver               │
│  ─────────────────────────────────  │
│  - m_pendingEdges : vector<PendingEdge>│
│  - m_clock : unique_ptr<IClock>     │
│  ─────────────────────────────────  │
│  + sendEdgeTrigger(reg) : Result    │
│  + servicePendingEdgeTriggers()     │
│  + pollFeedback(ctx)                │
│  + setClock(unique_ptr<IClock>)     │  ← 测试注入
│  + advanceTime(ms)                  │  ← 仅测试
│  + pendingEdgeCount() : size_t      │  ← 仅测试
└─────────────────────────────────────┘
```

### 附录 D：调用流程总览

```
Domain Layer (SystemManager)
  │
  │ send(SystemCommand)
  ▼
Infrastructure Layer (ModbusSystemDriver)
  │
  ├── send(SystemCommand) ──────────────────────────────────────────┐
  │   │                                                                │
  │   ├── [Level型]                                                    │
  │   │   └─ m_device.writeBool(reg, value)                           │
  │   │                                                                │
  │   ├── [ManualResetEdgeTrigger型]                                  │
  │   │   └─ sendEdgeTrigger(reg)                                    │
  │   │       ├─ m_device.writeBool(reg, true)   ← 立即发送 ON       │
  │   │       └─ m_pendingEdges.push_back(...)   ← 入队              │
  │   │                                                                │
  │   └── [AutoResetEdgeTrigger型]                                    │
  │       └─ m_device.writeBool(reg, true)  ← 仅发送 ON              │
  │                                                                    │
  ├── pollFeedback(ctx) ───────────────────────────────────────────┐  │
  │   │                                                              │  │
  │   ├── 1. servicePendingEdgeTriggers()                           │  │
  │   │   ├─ for each PendingEdge:                                 │  │
  │   │   │   if (now - onTime >= pulseWidthMs) {                  │  │
  │   │   │       m_device.writeBool(reg, false) ← 发送 OFF        │  │
  │   │   │       edge.offSent = true                              │  │
  │   │   │   }                                                     │  │
  │   │   └─ erase-remove(offSent == true)   ← 清理               │  │
  │   │                                                              │  │
  │   ├── 2. m_poller.poll()  → 批量读取 PLC                        │  │
  │   ├── 3. m_device.updateSnapshot()                              │  │
  │   ├── 4. isStateTrusted() 检查                                   │  │
  │   ├── 5. readAxisFeedback()  ×4 轴                              │  │
  │   └── 6. readSystemFeedback()                                   │  │
  │                                                                   │
  ▼                                                                   │
PLC (Modbus TCP)
```

---

> **文档版本**: v1.0  
> **最后更新**: 2026-05-30  
> **作者**: servoV6 架构组  
> **审核状态**: 待审核
