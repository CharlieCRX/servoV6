# CommunicationResult 重构实践文档

> **基于 ISystemDriver 重构设计说明（Cline 分析方案）的落地操作指南**
>
> 当前状态：`CommunicationResult` 结构体已在 `ISystemDriver.h` 中完整定义，`ISystemDriver::send()` 已返回 `CommunicationResult`，但所有 UseCase / Orchestrator 的调用方均**丢弃了返回值**。
>
> 目标：打通"驱动报告通讯错误 -> UseCase 传播 -> UI 诊断"的完整链路。

---

## 目录

1. [设计确认：CommunicationResult 结构体](#1-设计确认communicationresult-结构体)
2. [Modbus TCP 三层错误模型](#2-modbus-tcp-三层错误模型)
3. [现状态分析：谁在调用 send()，谁在丢弃返回值](#3-现状态分析谁在调用-send谁在丢弃返回值)
4. [分阶段实施计划](#4-分阶段实施计划)
5. [阶段 A：UseCase 层传播改造](#5-阶段-a-usecase-层传播改造)
6. [阶段 B：Orchestrator 层传播改造](#6-阶段-b-orchestrator-层传播改造)
7. [阶段 C：FakeAxisDriver 注入错误能力](#7-阶段-c-fakeaxisdriver-注入错误能力)
8. [阶段 D：测试覆盖](#8-阶段-d-测试覆盖)
9. [阶段 E：pollFeedback 失败策略文档化](#9-阶段-e-pollfeedback-失败策略文档化)
10. [附录 A：错误码映射表](#10-附录-a错误码映射表)
11. [附录 B：变更清单与检查表](#11-附录-b变更清单与检查表)

---

## 1. 设计确认：CommunicationResult 结构体

### 1.1 当前设计（ISystemDriver.h 中已有）

```cpp
struct CommunicationResult {
    enum class Status {
        Sent,            // 成功写入 PLC 寄存器
        NetworkError,    // TCP 连接失败 / 网线断开 / PLC 掉电
        Timeout,         // 通讯超时（Socket write/read 超时）
        Busy,            // PLC 忙（Modbus Exception 0x06）
        ProtocolError,   // Modbus 异常响应（非 Busy 的其他 Exception Code）
        InvalidResponse, // 返回数据非法（CRC 通过但数据格式不符合预期）
        Disconnected     // 当前未连接
    };

    Status status = Status::Sent;
    int exceptionCode = 0;        // Modbus Exception Code（仅在 ProtocolError 时有效）
    std::string diagnostic;       // 日志/UI 诊断信息

    [[nodiscard]] bool ok() const              { return status == Status::Sent; }
    [[nodiscard]] bool retryable() const       { /* Timeout | Busy -> true */ }
    [[nodiscard]] bool isNetworkIssue() const  { /* NetworkError | Timeout | Disconnected */ }
    [[nodiscard]] bool isProtocolIssue() const { return status == Status::ProtocolError; }
};
```

### 1.2 设计正确性确认

| 维度 | 评价 | 说明 |
|-----|------|------|
| 语义边界 | ✅ 正确 | 只表达"帧是否送达"，不表达"PLC 是否执行" |
| 错误层级 | ✅ 正确 | L1~L4 + L0 覆盖完整 |
| Modbus 异常码 | ✅ 正确 | `Busy` 单独抽出（因为可重试），其余归入 `ProtocolError` |
| retryable() | ✅ 正确 | Timeout/Busy -> true，其余 -> false，工业现场核心决策 |
| 实用方法 | ✅ 正确 | ok() / retryable() / isNetworkIssue() / isProtocolIssue() 各尽其职 |

**结论：设计无需修改，直接进入落地阶段。**

---

## 2. Modbus TCP 三层错误模型

```
                    ┌─────────────────────────────────┐
                    │          Qt 应用层               │
                    │  CommunicationResult.diagnostic  │
                    └──────────────┬──────────────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              │                    │                    │
     ┌────────▼────────┐  ┌───────▼───────┐  ┌────────▼────────┐
     │  Layer 3:       │  │  Layer 2:     │  │  Layer 1:       │
     │  Modbus 协议层  │  │  传输超时层   │  │  TCP 网络层     │
     │                 │  │               │  │                 │
     │  ProtocolError  │  │  Timeout      │  │  NetworkError   │
     │  Busy(0x06)     │  │               │  │  Disconnected   │
     │                 │  │               │  │                 │
     │  可重试: Busy   │  │  可重试: ✅   │  │  可重试: ❌     │
     │  其他: ❌       │  │               │  │                 │
     └────────┬────────┘  └───────┬───────┘  └────────┬────────┘
              │                    │                    │
              │  ┌─────────────────┘                    │
              │  │  ┌──────────────────────────────────┘
              │  │  │
     ┌────────▼──▼──▼────────┐
     │      汇川 PLC          │
     │  Modbus TCP Server     │
     └────────────────────────┘
```

### 2.1 第 1 层：TCP 网络层 -> `NetworkError`

| 现场现象 | 根因 | Status |
|---------|------|--------|
| 网线断开 | 物理层中断 | `NetworkError` |
| PLC 掉电 | 对端无响应 | `NetworkError` |
| socket connect 失败 | ECONNREFUSED | `NetworkError` |
| 连接被重置 | ECONNRESET | `NetworkError` |
| 主机不可达 | EHOSTUNREACH | `NetworkError` |
| 网络不可达 | ENETUNREACH | `NetworkError` |

**关键判断：NetworkError ≠ 可重试**。网线断开时重试无意义，应触发重连逻辑。

### 2.2 第 2 层：传输超时层 -> `Timeout`

| 现场现象 | 根因 | Status |
|---------|------|--------|
| PLC 扫描周期过长 | PLC 程序复杂 | `Timeout` |
| 交换机抖动 | 网络设备瞬时故障 | `Timeout` |
| PLC 负载高峰 | 多轴并发通讯 | `Timeout` |

**关键判断：Timeout ≠ 断线**。超时后网络可能立即恢复，属于可重试错误。

**工业现场事实：Timeout 非常常见，不等于断线。** 这是与 IT 系统最大的认知差异。

### 2.3 第 3 层：Modbus 协议层 -> `ProtocolError` / `Busy`

| Exception Code | 含义 | Status |
|---------------|------|--------|
| 0x01 | Illegal Function | `ProtocolError` |
| 0x02 | Illegal Address | `ProtocolError` |
| 0x03 | Illegal Value | `ProtocolError` |
| 0x06 | Server Device Busy | `Busy`（单独抽出） |

**关键判断：TCP 连接成功，PLC 返回了响应，但拒绝了请求。** 与 `NetworkError` 完全不同。

- `Busy` -> `retryable() = true`：PLC 显式告知忙，稍后重试
- `ProtocolError`（其他）-> `retryable() = false`：编程/配置错误，重试同样失败

### 2.4 数据层 -> `InvalidResponse`

CRC 校验通过但数据语义不符合预期：
- 响应长度与请求不匹配
- 寄存器值超出物理范围（如位置值 = NaN）
- 功能码与请求不一致

---

## 3. 现状态分析：谁在调用 send()，谁在丢弃返回值

### 3.1 调用方全景图

```
                         ISystemDriver::send(SystemCommand) -> CommunicationResult
                                     ↑
                                     │
        ┌────────────────────────────┼────────────────────────────┐
        │                            │                            │
   ┌────▼────┐  ┌──────────┐  ┌─────▼──────┐  ┌──────────────────▼───┐
   │ 6 个    │  │ 2 个     │  │ 1 个       │  │ 2 个 Policy          │
   │ UseCase │  │ Safety   │  │ Gantry     │  │ Orchestrator         │
   │ 文件    │  │ UseCase  │  │Orchestrator│  │                      │
   └────┬────┘  └────┬─────┘  └─────┬──────┘  └──────────┬───────────┘
        │            │              │                      │
        ▼            ▼              ▼                      ▼
   drv->send(...)  drv->send(...) drv->send(...)    间接通过 UseCase
   返回值被丢弃   返回值被丢弃    返回值被丢弃        返回值被丢弃
```

### 3.2 逐文件分析

| 文件 | send() 调用位置 | 当前行为 | 是否捕获返回值 |
|-----|----------------|---------|:---:|
| `EnableUseCase.h` | 第 103 行 `drv->send(AxisCommandWithId{...})` | 直接丢弃 | ❌ |
| `JogAxisUseCase.h` | execute() 第 66 行, stop() 第 114 行 | 直接丢弃 | ❌ |
| `MoveAbsoluteUseCase.h` | 第 67 行 | 直接丢弃 | ❌ |
| `MoveRelativeUseCase.h` | 第 66 行 | 直接丢弃 | ❌ |
| `StopAxisUseCase.h` | 第 58 行 | 直接丢弃 | ❌ |
| `EmergencyStopUseCase.h` | 第 74 行 | 直接丢弃 | ❌ |
| `ReleaseEmergencyStopUseCase.h` | 第 73 行 | 直接丢弃 | ❌ |
| `GantryOrchestrator.h` | tick() 第 94 行, 第 135 行, 第 157 行 | 直接丢弃 | ❌ |
| `AutoAbsMoveOrchestrator.h` | 间接通过 Enable/MoveAbs UseCase | 间接丢弃 | ❌ |
| `AutoRelMoveOrchestrator.h` | 间接通过 Enable/MoveRel UseCase | 间接丢弃 | ❌ |

### 3.3 影响分析

**共 11 处 send() 调用，10 处丢弃返回值。**

风险：如果物理驱动（ModbusTcpAxisDriver）返回 `NetworkError` 或 `Timeout`，UseCase 层完全不知情，导致：
- 命令未送达 PLC 但系统认为"已下发"
- UI 不显示任何错误
- 后续运动编排器继续执行，状态机卡死

---

## 4. 分阶段实施计划

```
阶段 A ──────► 阶段 B ──────► 阶段 C ──────► 阶段 D ──────► 阶段 E
UseCase 改造   Orchestrator   Fake 注入      测试覆盖       pollFeedback
send 返回值    传播改造       错误能力                      文档化
传播
```

### 4.1 阶段总览

| 阶段 | 内容 | 涉及文件 | 风险 | 预计工时 |
|-----|------|---------|------|---------|
| A | UseCase 层传播 | 8 个 .h | 低 -- 接口兼容 | 1.5h |
| B | Orchestrator 传播 | 3 个 .h | 中 -- 需新增错误类型 | 1h |
| C | Fake 注入错误能力 | 1 个 .h | 低 -- 纯新增 | 0.5h |
| D | 测试覆盖 | 1~2 个 .cpp | 低 -- 新增测试 | 1.5h |
| E | pollFeedback 文档化 | 1 个 .h | 无 -- 纯注释 | 0.5h |

### 4.2 依赖关系

```
阶段 A（UseCase 传播）── 被阶段 B 依赖 ──► 阶段 B（Orchestrator 传播）
                                                    │
阶段 C（Fake 注入）  ── 被阶段 D 依赖 ──► 阶段 D（测试覆盖）
```

- 阶段 A 和阶段 B 有依赖关系（Orchestrator 调用 UseCase）
- 阶段 C 和阶段 D 有依赖关系（测试需要注入能力）
- A/B 与 C/D 可并行推进

---

## 5. 阶段 A：UseCase 层传播改造

### 5.1 核心原则

**不修改 UseCase 的公共接口签名。** 所有 UseCase 的 `execute()` 方法返回 `UseCaseError`，这是应用层统一的错误类型。通讯错误应映射为 `UseCaseError` 中的现有类型，或在 `UseCaseError` variant 中新增一个 `CommunicationResult` 分支。

### 5.2 方案选择

**方案 1（推荐）：在 UseCaseError variant 中增加 CommunicationResult**

```cpp
using UseCaseError = std::variant<
    std::monostate,
    ContextRejection,
    RejectionReason,
    GantryRejection,
    SafetyRejection,
    CommunicationResult    // ← 新增：通讯层错误
>;
```

优点：
- 不破坏现有测试（所有 `std::holds_alternative<T>` 检查继续有效）
- 通讯错误保持原始语义，UI 可直接读取 `diagnostic` 字段
- 调用方可以通过 `result.retryable()` 决策是否重试

缺点：
- 增加 `UseCaseError.h` 对 `infrastructure/ISystemDriver.h` 的依赖（Application -> Infrastructure）

**方案 2（备选）：将 CommunicationResult 映射为现有错误类型**

```cpp
ContextRejection mapToCtxRejection(const CommunicationResult& r) {
    switch (r.status) {
        case CommunicationResult::Status::NetworkError:
        case CommunicationResult::Status::Disconnected:
            return ContextRejection::TransportError;  // 需新增
        case CommunicationResult::Status::Timeout:
            return ContextRejection::TimeoutError;    // 需新增
        // ...
    }
}
```

缺点：丢失 `diagnostic` 和 `exceptionCode` 信息，UI 无法展示具体诊断。

**选定：方案 1。**

### 5.3 UseCaseError.h 修改

文件：`application/UseCaseError.h`

```cpp
#pragma once
#include <variant>
#include "entity/ContextRejection.h"
#include "entity/Axis.h"
#include "gantry/GantryRejection.h"
#include "safety/SafetyRejection.h"
#include "infrastructure/ISystemDriver.h"  // CommunicationResult

using UseCaseError = std::variant<
    std::monostate,         // 成功
    ContextRejection,       // SystemManager / SystemContext 层
    RejectionReason,        // Axis 领域层
    GantryRejection,        // Gantry 联动层
    SafetyRejection,        // 安全域急停层
    CommunicationResult     // 通讯层错误（来自 ISystemDriver）
>;
```

### 5.4 各 UseCase 修改模板

以 `EnableUseCase.h` 为例：

**修改前：**
```cpp
if (axis->hasPendingCommand()) {
    if (auto* drv = group->driver()) {
        drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
    }
}
return std::monostate{};
```

**修改后：**
```cpp
if (axis->hasPendingCommand()) {
    if (auto* drv = group->driver()) {
        auto result = drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
        if (!result.ok()) {
            return result;  // CommunicationResult is now part of UseCaseError
        }
    }
}
return std::monostate{};
```

### 5.5 逐个 UseCase 的修改说明

| UseCase | 文件 | 修改行 | 特殊说明 |
|---------|------|-------|---------|
| EnableUseCase | `application/axis/EnableUseCase.h` | ~103 | 标准模板 |
| JogAxisUseCase | `application/axis/JogAxisUseCase.h` | execute(): ~66, stop(): ~114 | stop() 当前返回 void，需要改为返回 UseCaseError 或保持 void（stop 是安全操作，通讯失败不应阻止状态更新） |
| MoveAbsoluteUseCase | `application/axis/MoveAbsoluteUseCase.h` | ~67 | 标准模板 |
| MoveRelativeUseCase | `application/axis/MoveRelativeUseCase.h` | ~66 | 标准模板 |
| StopAxisUseCase | `application/axis/StopAxisUseCase.h` | ~58 | Stop 是安全操作（不可拒绝），但通讯失败仍需返回错误 |
| EmergencyStopUseCase | `application/safety/EmergencyStopUseCase.h` | ~74 | 标准模板 |
| ReleaseEmergencyStopUseCase | `application/safety/ReleaseEmergencyStopUseCase.h` | ~73 | 标准模板 |

### 5.6 JogAxisUseCase::stop() 的特殊处理

当前 `JogAxisUseCase::stop()` 返回 `void`。需要决定是否改为返回 `UseCaseError`。

**建议：保持 `void`，但在内部记录日志。** 因为：
1. stop 是安全指令，调用方（通常是 UI 松开按钮）不应因通讯失败而收到错误弹窗
2. 如果通讯失败导致 stop 命令未送达，下一帧 pollFeedback 会反映轴仍在运动
3. 这是工业控制的常见模式：安全操作不阻塞 UI

```cpp
void stop(SystemManager& manager,
          const std::string& groupName,
          AxisId axisId,
          Direction dir) {
    // ... 前面的逻辑不变 ...
    if (axis->stopJog(dir)) {
        if (auto* drv = group->driver()) {
            auto result = drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
            if (!result.ok()) {
                LOG_WARN(LogLayer::HAL, "JogUC",
                    "Stop command delivery failed: " + result.diagnostic);
                // 不返回错误 -- stop 是安全操作，下一帧 feedback 会反映真实状态
            }
        }
    }
}
```

---

## 6. 阶段 B：Orchestrator 层传播改造

### 6.1 GantryOrchestrator

文件：`application/policy/GantryOrchestrator.h`

共 3 处 `drv->send(...)` 调用，当前全部丢弃返回值。

**修改策略：** Orchestrator 的 `tick()` 中，如果 send 返回错误，则将 `m_step` 置为 `Error`，并将 `m_lastError` 设置为 `CommunicationResult`。

```cpp
// 修改前（以 EnsuringEnabled 为例）
if (power.hasPendingCommand() && drv) {
    drv->send(power.popPendingCommand());
}

// 修改后
if (power.hasPendingCommand() && drv) {
    auto result = drv->send(power.popPendingCommand());
    if (!result.ok()) {
        m_step = Step::Error;
        m_lastError = result;
        return;
    }
}
```

3 处修改位置：
1. `Case::EnsuringEnabled` -- 第 94 行，GantryPowerCommand
2. `Case::Coupling` -- 第 135 行，GantryCouplingCommand
3. `Case::Decoupling` -- 第 157 行，GantryCouplingCommand

### 6.2 AutoAbsMoveOrchestrator 和 AutoRelMoveOrchestrator

这两个 Orchestrator 间接通过 `EnableUseCase` 和 `MoveUseCase` 调用 `drv->send()`。

当阶段 A 完成后，UseCase 已经会返回 `CommunicationResult`。Orchestrator 的 tick() 中已有 `m_lastError = err` 模式，因此**自动受益，无需额外修改**。

验证点：确认 `EnableUseCase` / `MoveUseCase` 的通讯错误能正确被 Orchestrator 的 `tick()` 中 `m_lastError = err` 捕获。

### 6.3 JogOrchestrator 与 AutoOrchestrator

**JogOrchestrator：** 间接通过 `JogAxisUseCase` 调用 `drv->send()`。当阶段 A 完成后，JogAxisUseCase 已经会返回 `CommunicationResult`，JogOrchestrator 自动受益。但 JogOrchestrator 的 tick() 中需要检查 JogAxisUseCase::execute() 的返回值是否为 `CommunicationResult` 并据此进入 Error 状态。

**AutoAbsMoveOrchestrator / AutoRelMoveOrchestrator：** 同上述逻辑，间接通过 `EnableUseCase` / `MoveUseCase` 受益，无需额外修改。

**验证点：** 确保各 Orchestrator 的 `tick()` 中 `m_lastError = err` 能正确处理 `CommunicationResult` 类型。

---

## 7. 阶段 C：FakeAxisDriver 注入错误能力

### 7.1 当前状态

`FakeAxisDriver::send()` 在未连接时返回 `Disconnected`，连接时永远返回 `Sent{}`。

### 7.2 需要新增的能力

```cpp
class FakeAxisDriver : public ISystemDriver {
public:
    // ... existing code ...

    // ========== 错误注入（测试专用） ==========

    /// @brief 注入下一次 send() 的返回值（单次消费，消费后恢复为 Sent）
    void injectNextSendResult(CommunicationResult result) {
        m_injectedResult = result;
        m_injectEnabled = true;
    }

    /// @brief 注入持续的 send() 返回值（直到被 clearInjectedResult 清除）
    void injectPersistentSendResult(CommunicationResult result) {
        m_injectedResult = result;
        m_injectEnabled = true;
        m_injectPersistent = true;
    }

    /// @brief 清除注入，恢复正常行为
    void clearInjectedResult() {
        m_injectEnabled = false;
        m_injectPersistent = false;
        m_injectedResult = CommunicationResult{};
    }

private:
    bool m_injectEnabled = false;
    bool m_injectPersistent = false;
    CommunicationResult m_injectedResult{};
};
```

修改 `send()` 方法：

```cpp
CommunicationResult send(const SystemCommand& cmd) override {
    if (!m_connected) {
        return CommunicationResult{
            CommunicationResult::Status::Disconnected,
            0,
            "Fake driver not connected"
        };
    }

    // === 错误注入检查 ===
    if (m_injectEnabled) {
        if (!m_injectPersistent) {
            m_injectEnabled = false;  // 单次消费
        }
        return m_injectedResult;
    }

    std::visit([this](auto&& c) { handle(c); }, cmd);
    return CommunicationResult{};  // Fake 驱动永远通讯成功
}
```

---

## 8. 阶段 D：测试覆盖

### 8.1 新增测试文件建议

在 `tests/infrastructure/` 下新建 `test_communication_error.cpp`。

### 8.2 测试用例清单

| 测试用例 | 测试内容 | 优先级 |
|---------|---------|:---:|
| `ShouldReturnNetworkErrorOnDisconnect` | disconnect() 后 send() 返回 Disconnected | 高 |
| `ShouldPropagateErrorFromEnableUseCase` | 注入 NetworkError -> EnableUseCase 返回 CommunicationResult | 高 |
| `ShouldPropagateErrorFromMoveUseCase` | 注入 Timeout -> MoveAbsoluteUseCase 返回 CommunicationResult | 高 |
| `ShouldPropagateErrorFromEmergencyStop` | 注入 ProtocolError -> EmergencyStopUseCase 返回 CommunicationResult | 高 |
| `ShouldRetryOnTimeout` | 注入 Timeout -> retryable() = true -> 重试后成功 | 中 |
| `ShouldNotRetryOnNetworkError` | 注入 NetworkError -> retryable() = false | 中 |
| `ShouldPropagateBusyWithRetry` | 注入 Busy(0x06) -> retryable() = true | 中 |
| `ShouldPropagateProtocolErrorWithCode` | 注入 ProtocolError(0x02) -> exceptionCode = 0x02 | 中 |
| `ShouldGantryOrchestratorFailOnSendError` | Gantry 流程中注入 NetworkError -> Step::Error | 高 |
| `ShouldJogStopNotBlockOnSendError` | Jog::stop() 通讯失败不阻塞（void 返回） | 中 |
| `ShouldDiagnosticBeHumanReadable` | diagnostic 包含 IP 和错误描述 | 低 |

### 8.3 测试代码模板

```cpp
#include <gtest/gtest.h>
#include "domain/entity/Axis.h"
#include "domain/entity/SystemContext.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include "application/SystemManager.h"
#include "application/axis/EnableUseCase.h"
#include "application/axis/MoveAbsoluteUseCase.h"

class CommunicationErrorTest : public ::testing::Test {
protected:
    FakePLC plc;
    FakeAxisDriver driver{plc};
    SystemManager manager;
    SystemContext* ctx = nullptr;

    EnableUseCase enableUc;
    MoveAbsoluteUseCase moveAbsUc;

    void SetUp() override {
        ContextRejection reason;
        ASSERT_TRUE(manager.createGroup("TestGroup", reason));
        ASSERT_TRUE(manager.tryGetGroup("TestGroup", ctx, reason));
        ctx->setDriver(&driver);

        plc.forceState(AxisId::Y, AxisState::Disabled);
        plc.setSimulatedMoveVelocity(AxisId::Y, 50.0);

        // 初始反馈同步
        Axis* a = nullptr;
        ContextRejection r;
        ctx->tryGetAxis(AxisId::Y, a, r);
        a->applyFeedback(plc.getFeedback(AxisId::Y));
    }
};

// ============================================================================
// 测试 1：断线时 EnableUseCase 应返回 CommunicationResult::Disconnected
// ============================================================================
TEST_F(CommunicationErrorTest, ShouldPropagateDisconnectedFromEnableUseCase) {
    driver.disconnect();

    auto result = enableUc.execute(manager, "TestGroup", AxisId::Y, true);

    ASSERT_TRUE(std::holds_alternative<CommunicationResult>(result));
    auto commResult = std::get<CommunicationResult>(result);
    EXPECT_EQ(commResult.status, CommunicationResult::Status::Disconnected);
    EXPECT_FALSE(commResult.ok());
    EXPECT_FALSE(commResult.retryable());
}

// ============================================================================
// 测试 2：注入 Timeout 后应标记为可重试
// ============================================================================
TEST_F(CommunicationErrorTest, ShouldMarkTimeoutAsRetryable) {
    // 先使能（通讯成功）
    auto enableResult = enableUc.execute(manager, "TestGroup", AxisId::Y, true);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(enableResult));

    // 推进到 Idle
    for (int i = 0; i < 20; ++i) {
        plc.tick(10);
        Axis* a = nullptr;
        ContextRejection r;
        ctx->tryGetAxis(AxisId::Y, a, r);
        a->applyFeedback(plc.getFeedback(AxisId::Y));
    }

    // 注入 Timeout
    driver.injectNextSendResult(CommunicationResult{
        CommunicationResult::Status::Timeout,
        0,
        "192.168.1.100:502 timeout after 500ms"
    });

    auto moveResult = moveAbsUc.execute(manager, "TestGroup", AxisId::Y, 100.0);

    ASSERT_TRUE(std::holds_alternative<CommunicationResult>(moveResult));
    auto commResult = std::get<CommunicationResult>(moveResult);
    EXPECT_EQ(commResult.status, CommunicationResult::Status::Timeout);
    EXPECT_TRUE(commResult.retryable());  // ← 核心断言
}

// ============================================================================
// 测试 3：注入 ProtocolError 应保留 exceptionCode
// ============================================================================
TEST_F(CommunicationErrorTest, ShouldPreserveExceptionCodeOnProtocolError) {
    // 注入 Modbus Exception 0x02 (Illegal Address)
    driver.injectNextSendResult(CommunicationResult{
        CommunicationResult::Status::ProtocolError,
        0x02,
        "Modbus Exception: Illegal Data Address"
    });

    auto result = enableUc.execute(manager, "TestGroup", AxisId::Y, true);

    ASSERT_TRUE(std::holds_alternative<CommunicationResult>(result));
    auto commResult = std::get<CommunicationResult>(result);
    EXPECT_EQ(commResult.status, CommunicationResult::Status::ProtocolError);
    EXPECT_EQ(commResult.exceptionCode, 0x02);
    EXPECT_FALSE(commResult.retryable());
    EXPECT_TRUE(commResult.isProtocolIssue());
}

// ============================================================================
// 测试 4：GantryOrchestrator 在 send 失败时进入 Error
// ============================================================================
TEST_F(CommunicationErrorTest, ShouldGantryOrchFailOnNetworkError) {
    GantryOrchestrator orch(manager, "TestGroup");

    // 注入 NetworkError
    driver.injectPersistentSendResult(CommunicationResult{
        CommunicationResult::Status::NetworkError,
        0,
        "Connection lost"
    });

    orch.startCoupling();
    orch.tick();

    EXPECT_TRUE(orch.hasError());
    EXPECT_EQ(orch.currentStep(), GantryOrchestrator::Step::Error);

    auto err = orch.lastError();
    ASSERT_TRUE(std::holds_alternative<CommunicationResult>(err));
    EXPECT_EQ(std::get<CommunicationResult>(err).status,
              CommunicationResult::Status::NetworkError);

    driver.clearInjectedResult();
}
```

---

## 9. 阶段 E：pollFeedback 失败策略文档化

`pollFeedback()` 当前签名返回 `void`。在通讯失败时，pollFeedback 应**保留上次已知值，不更新**。

此项已在 `ISystemDriver.h` 的注释中声明，但需要在所有驱动实现中确保一致性。

### 9.1 失败的策略（已在 ISystemDriver.h 注释中声明）

```
失败策略: 通信失败时保留上次已知反馈值，不更新，记 TRACE_WARN
```

### 9.2 FakeAxisDriver 当前状态

`FakeAxisDriver::pollFeedback()` 当前不检查通讯状态，总是推进 FakePLC 并分发反馈。这是因为 Fake 环境默认通讯成功。

### 9.3 生产驱动的预期实现

```cpp
// ModbusTcpAxisDriver::pollFeedback() 的预期骨架
void ModbusTcpAxisDriver::pollFeedback(SystemContext& ctx) {
    if (!m_connected) {
        // 保留上次已知值，不更新任何领域状态
        TRACE_WARN("HAL", "Driver", "pollFeedback skipped: not connected");
        return;
    }

    // 读取 Holding Registers
    auto result = m_modbusClient.readHoldingRegisters(...);
    if (!result.ok()) {
        TRACE_WARN("HAL", "Driver",
            "pollFeedback failed: " + result.diagnostic);
        // 保留上次已知值
        return;
    }

    // ... 正常分发 ...
}
```

### 9.4 需要文档化的内容

在 `ISystemDriver.h` 的 `pollFeedback()` 注释中进一步细化：

```cpp
/**
 * @brief 从硬件拉取反馈并分发给 SystemContext 内的所有领域实体
 *
 * 每主循环周期调用一次（典型周期: 10ms）
 *
 * 内部执行:
 *   1. Read:  从硬件读取当前状态
 *   2. Translate: 硬件数据 -> 领域反馈结构体
 *   3. Dispatch: 注入 axis->applyFeedback() / emergencyStopController.applyFeedback() / ...
 *
 * 失败策略（重要）:
 *   - 通讯失败时【保留上次已知反馈值，不更新领域状态】
 *   - 不能将反馈值置零或置为默认值----这会导致领域实体状态跳变
 *   - 典型的错误场景:
 *     * 单次 TCP read 超时（偶尔发生）
 *     * 连续 TCP read 超时（需触发断线检测）
 *     * Modbus 异常响应（协议层错误）
 *   - 每次失败记录 TRACE_WARN（不是 ERROR----单次超时是正常现象）
 *   - 连续失败超过阈值（如 500ms）时记录 ERROR，触发重连
 *
 * @param ctx 目标分组上下文
 */
virtual void pollFeedback(SystemContext& ctx) = 0;
```

---

## 10. 附录 A：错误码映射表

### 10.1 CommunicationResult -> 用户可见信息

| Status | diagnostic 示例 | UI 展示 | 用户操作 |
|--------|----------------|---------|---------|
| `Sent` | -- | 正常 | 无 |
| `NetworkError` | `"192.168.1.100:502 ECONNREFUSED"` | "PLC 连接失败" | 检查网线/PLC 电源 |
| `Timeout` | `"192.168.1.100:502 timeout 500ms"` | "通讯超时" | 等待或减少并发操作 |
| `Busy` | `"Modbus Exception 0x06: Device Busy"` | "PLC 忙，自动重试中" | 等待 |
| `ProtocolError` | `"Modbus Exception 0x02: Illegal Address"` | "协议错误 (0x02)" | 检查程序配置 |
| `InvalidResponse` | `"Unexpected response length: 4 != 8"` | "数据异常" | 联系技术支持 |
| `Disconnected` | `"Driver not initialized"` | "驱动未连接" | 检查驱动初始化 |

### 10.2 Modbus Exception Code 完整表

| Code | 名称 | 含义 | Status | retryable |
|------|------|------|--------|:---------:|
| 0x01 | Illegal Function | 功能码不支持 | `ProtocolError` | ❌ |
| 0x02 | Illegal Data Address | 寄存器地址非法 | `ProtocolError` | ❌ |
| 0x03 | Illegal Data Value | 数据值非法 | `ProtocolError` | ❌ |
| 0x04 | Slave Device Failure | 从站设备故障 | `ProtocolError` | ❌ |
| 0x05 | Acknowledge | 已确认但需时间 | `Busy` | ✅ |
| 0x06 | Server Device Busy | 设备忙 | `Busy` | ✅ |
| 0x07 | NAK | 否定应答 | `ProtocolError` | ❌ |
| 0x08 | Memory Parity Error | 内存奇偶校验错误 | `ProtocolError` | ❌ |
| 0x0A | Gateway Path Unavailable | 网关路径不可用 | `ProtocolError` | ❌ |
| 0x0B | Gateway Target No Response | 目标设备无响应 | `NetworkError` | ❌ |

> 注：汇川 PLC 通常仅使用 0x01 / 0x02 / 0x03 / 0x06。

---

## 11. 附录 B：变更清单与检查表

### 11.1 完整文件变更清单

| 文件 | 操作 | 阶段 |
|-----|------|:---:|
| `application/UseCaseError.h` | 修改：增加 `CommunicationResult` 分支 + `#include` | A |
| `application/axis/EnableUseCase.h` | 修改：捕获 send() 返回值 | A |
| `application/axis/JogAxisUseCase.h` | 修改：execute() 捕获返回值，stop() 记录日志 | A |
| `application/axis/MoveAbsoluteUseCase.h` | 修改：捕获 send() 返回值 | A |
| `application/axis/MoveRelativeUseCase.h` | 修改：捕获 send() 返回值 | A |
| `application/axis/StopAxisUseCase.h` | 修改：捕获 send() 返回值 | A |
| `application/safety/EmergencyStopUseCase.h` | 修改：捕获 send() 返回值 | A |
| `application/safety/ReleaseEmergencyStopUseCase.h` | 修改：捕获 send() 返回值 | A |
| `application/policy/GantryOrchestrator.h` | 修改：3 处 send() 调用捕获返回值 | B |
| `infrastructure/FakeAxisDriver.h` | 修改：增加 `injectNextSendResult()` 等方法 | C |
| `infrastructure/ISystemDriver.h` | 修改：增强 pollFeedback() 注释 | E |
| `tests/infrastructure/test_communication_error.cpp` | **新建**：通讯错误传播测试 | D |

### 11.2 检查表（逐项完成后打勾）

#### 阶段 A -- UseCase 层
- [ ] `UseCaseError.h` 增加 `CommunicationResult` 分支
- [ ] `EnableUseCase.h` 发送后检查 `result.ok()`
- [ ] `JogAxisUseCase.h` execute() 发送后检查 `result.ok()`
- [ ] `JogAxisUseCase.h` stop() 发送后记录 LOG_WARN（保持 void 返回）
- [ ] `MoveAbsoluteUseCase.h` 发送后检查 `result.ok()`
- [ ] `MoveRelativeUseCase.h` 发送后检查 `result.ok()`
- [ ] `StopAxisUseCase.h` 发送后检查 `result.ok()`
- [ ] `EmergencyStopUseCase.h` 发送后检查 `result.ok()`
- [ ] `ReleaseEmergencyStopUseCase.h` 发送后检查 `result.ok()`
- [ ] 编译通过（`cmake --build build`）

#### 阶段 B -- Orchestrator 层
- [ ] `GantryOrchestrator.h` EnsuringEnabled 步骤捕获 send() 返回值
- [ ] `GantryOrchestrator.h` Coupling 步骤捕获 send() 返回值
- [ ] `GantryOrchestrator.h` Decoupling 步骤捕获 send() 返回值
- [ ] 编译通过

#### 阶段 C -- Fake 注入
- [ ] `FakeAxisDriver.h` 增加 `injectNextSendResult()`
- [ ] `FakeAxisDriver.h` 增加 `injectPersistentSendResult()`
- [ ] `FakeAxisDriver.h` 增加 `clearInjectedResult()`
- [ ] `FakeAxisDriver::send()` 增加注入检查逻辑
- [ ] 编译通过

#### 阶段 D -- 测试覆盖
- [ ] 新建 `tests/infrastructure/test_communication_error.cpp`
- [ ] `ShouldPropagateDisconnectedFromEnableUseCase` 测试
- [ ] `ShouldMarkTimeoutAsRetryable` 测试
- [ ] `ShouldPreserveExceptionCodeOnProtocolError` 测试
- [ ] `ShouldGantryOrchFailOnNetworkError` 测试
- [ ] `ShouldJogStopNotBlockOnSendError` 测试
- [ ] `ctest` 全部通过

#### 阶段 E -- 文档化
- [ ] `ISystemDriver.h` pollFeedback() 注释增强
- [ ] 本重构实践文档归档

### 11.3 回归测试检查

完成所有阶段后，运行全量测试：

```bash
cd build
cmake --build .
ctest --output-on-failure
```

确保所有已有测试继续通过，新增测试全部通过。

---

> **文档版本**: v1.0
> **创建日期**: 2026-05-15
> **关联文档**: `ISystemDriver重构设计说明----Cline分析方案.md`、`统一命令总线与反馈分发 -- 架构重构思考.md`
