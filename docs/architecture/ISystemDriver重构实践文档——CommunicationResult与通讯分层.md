# ISystemDriver 重构实践文档 — CommunicationResult 与 Modbus TCP 通讯分层

> 版本: v3.0  
> 日期: 2026-05-15  
> 状态: **阶段 A/B 已完成，阶段 C–H 待执行**  
> 基于原分析方案 v2.0 的细化升级，适配真实工业现场的 Modbus TCP 错误分层模型

---

## 目录

1. [设计原理：Modbus TCP 的层级化错误模型](#1-设计原理modbus-tcp-的层级化错误模型)
2. [CommunicationResult 终极设计](#2-communicationresult-终极设计)
3. [已完成工作 (阶段 A–B)](#3-已完成工作-阶段-a-b)
4. [待执行：分阶段实施计划 (阶段 C–H)](#4-待执行分阶段实施计划-阶段-c-h)
5. [各阶段详细变更清单](#5-各阶段详细变更清单)
6. [测试适配指南](#6-测试适配指南)
7. [生产驱动骨架 (ModbusTcpAxisDriver)](#7-生产驱动骨架-modbustcpaxisdriver)
8. [附录：调用链全景图](#8-附录调用链全景图)

---

## 1. 设计原理：Modbus TCP 的层级化错误模型

### 1.1 为什么原分析方案的 CommunicationResult 不够用

原分析方案 (§4.1) 提出三态模型：

```cpp
enum class Status { Sent, Failed, Busy };
```

这个模型有两个不足：

1. **`Failed` 太粗粒度**。在工业现场，"网线断开"和"Modbus 返回 0x01 Illegal Function"是两种截然不同的问题——前者是物理层/网络层故障，后者是应用层协议拒绝。把它们统一标记为 `Failed`，丢失了**故障定位的关键信息**。

2. **缺少 `Timeout`**。在工业控制中，"超时"是一个非常特殊的状态——它不等于断线（NetworkError），也不等于 PLC 拒绝（ProtocolError）。PLC 可能只是扫描周期过长，超时后下一秒就能正常响应。**Timeout 应该是 `retryable` 的**。

3. **缺少 `Disconnected`**。"从未连接过"与"连接后意外断线"在 UI 层需要不同提示。`Disconnected` 表示驱动处于未初始化/已显式断开状态，与 `NetworkError`（已连接但通信尝试中失败）语义不同。

### 1.2 工业现场的三层错误模型

```
┌──────────────────────────────────────────────────────────────┐
│                      Qt 应用层                               │
│                 UseCase / Orchestrator                       │
│                                                              │
│   根据 CommunicationResult 决定:                             │
│     - ok()         → 继续 (通讯成功)                         │
│     - retryable()  → 重试 (Timeout/Busy)                    │
│     - !retryable() → 报错并终止当前操作                      │
└───────────────────────┬──────────────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────────────┐
│                   Modbus TCP 驱动层                           │
│              ISystemDriver::send()                           │
│                                                              │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐     │
│  │  L1 网络层   │ → │  L2 传输层   │ → │  L3 应用层   │     │
│  │              │   │              │   │              │     │
│  │ connect()    │   │ write()      │   │ 解析响应     │     │
│  │ ECONNREFUSED │   │ recv() 超时  │   │ Exception    │     │
│  │ EHOSTUNREACH │   │              │   │ Code 0x01~   │     │
│  │              │   │              │   │ 0x06         │     │
│  └──────┬───────┘   └──────┬───────┘   └──────┬───────┘     │
│         ▼                  ▼                  ▼              │
│  NetworkError         Timeout          ProtocolError         │
│  Disconnected                         Busy                   │
│                                       InvalidResponse        │
└──────────────────────────────────────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────────────┐
│                   汇川 PLC (H3U/H5U)                         │
│                                                              │
│   接收 Modbus 帧 → 校验 CRC → 写入寄存器 → 返回响应          │
│   异常时返回 Exception Code                                  │
└──────────────────────────────────────────────────────────────┘
```

### 1.3 各错误状态的工业语义

| Status | 含义 | 典型场景 | retryable | 行号/日志级别 |
|--------|------|---------|-----------|-------------|
| `Sent` | 通讯成功 | Modbus 正常响应 | — | TRACE |
| `NetworkError` | TCP 连接失败 | 网线断开、PLC 掉电、ECONNRESET、ECONNREFUSED | ❌ | ERROR |
| `Timeout` | 通讯超时 | PLC 忙导致响应延迟、交换机抖动、扫描周期过长 | ✅ | WARN |
| `Busy` | PLC 忙 | Modbus Exception 0x06 (Server Device Busy) | ✅ | INFO |
| `ProtocolError` | Modbus 异常响应 | 0x01 Illegal Function, 0x02 Illegal Address, 0x03 Illegal Value | ❌ | ERROR |
| `InvalidResponse` | 返回数据非法 | CRC 通过但数据长度不对、寄存器值超出范围 | ❌ | ERROR |
| `Disconnected` | 当前未连接 | 驱动未初始化或已显式断开 | ❌ | WARN |

**关键区分：Timeout ≠ NetworkError**

在工业现场，Timeout 非常常见：

```
场景: PLC 正在执行扫描周期（典型 1–10ms）
  T0: Qt 发送 Modbus 请求
  T1: 请求到达 PLC 网口
  T2: PLC 正在扫描 ladder 程序（需要 3ms）
  T3: PLC 处理 Modbus 请求（1ms）
  T4: PLC 返回响应
  
  如果 Qt 端的 recv timeout 设为 3ms:
  → T1 到 T4 共 4ms > 3ms → Timeout
  
  此时 TCP 连接完全正常！
  下次通信（隔 10ms 后）很可能立即成功。
```

如果在 Timeout 后标记为 NetworkError 并触发重连，会导致：
- 不必要的 TCP 断连/重连（耗时 100ms+）
- 丢失在途的 Modbus 请求
- 工业现场操作员的困惑

### 1.4 retryable() 的决策逻辑

```cpp
bool retryable() const {
    switch (status) {
        case Status::Timeout:  // PLC 只是慢，稍后重试大概率成功
        case Status::Busy:     // PLC 显式告知忙，等待后重试
            return true;
        default:
            return false;      // 其他错误重试无意义
    }
}
```

**为什么不重试 NetworkError？**

网线断开后，重试 3 次 socket connect 不会有任何效果。正确做法是：
1. 立即返回 NetworkError 给上层
2. UI 显示"网络连接失败，请检查网线"
3. 驱动记录 ERROR 日志
4. 等待操作员排查物理问题

**为什么不重试 ProtocolError？**

Modbus Exception 0x02 (Illegal Address) 意味着寄存器地址配置错误。重试 100 次结果都一样。正确做法是：
1. 返回 ProtocolError + exceptionCode 给上层
2. 触发配置自检/回读校验
3. 必要时停止设备进入安全状态

---

## 2. CommunicationResult 终极设计

### 2.1 完整结构（已在阶段 A 实现）

```cpp
// infrastructure/ISystemDriver.h

struct CommunicationResult {

    enum class Status {
        Sent,             // 成功写入 PLC 寄存器
        NetworkError,     // TCP 连接失败 / 网线断开 / PLC 掉电
        Timeout,          // 通讯超时（不等于断线，可重试）
        Busy,             // PLC 忙（Modbus Exception 0x06，可重试）
        ProtocolError,    // Modbus 异常响应（保留 exceptionCode）
        InvalidResponse,  // 返回数据非法（CRC 通过但格式不对）
        Disconnected      // 当前未连接（驱动未初始化/已断开）
    };

    Status status = Status::Sent;
    int exceptionCode = 0;           // Modbus Exception Code（ProtocolError 时有效）
    std::string diagnostic;          // 诊断信息（日志/UI 用）

    [[nodiscard]] bool ok() const;              // status == Sent
    [[nodiscard]] bool retryable() const;       // Timeout || Busy
    [[nodiscard]] bool isNetworkIssue() const;  // NetworkError || Timeout || Disconnected
    [[nodiscard]] bool isProtocolIssue() const; // ProtocolError
};
```

### 2.2 便捷判断方法的设计意图

| 方法 | 用途 | 使用者 |
|------|------|--------|
| `ok()` | 快速判断通讯是否成功 | UseCase execute() 末尾 |
| `retryable()` | 判断是否应该重试 | Orchestrator 重试循环 |
| `isNetworkIssue()` | 统一处理所有网络层问题 | 日志模块 / UI 诊断面板 |
| `isProtocolIssue()` | 区分 Modbus 层问题 | 配置自检逻辑 |

---

## 3. 已完成工作 (阶段 A–B)

### 3.1 阶段 A: ISystemDriver.h ✅

**文件**: `infrastructure/ISystemDriver.h`

**变更内容**:
1. 新增 `CommunicationResult` 结构体（含枚举 7 状态 + 4 便捷方法）
2. `ISystemDriver::send()` 返回值从 `void` 改为 `CommunicationResult`
3. 新增 `ISystemDriver::pollFeedback(SystemContext&)` 纯虚方法
4. 新增 `SystemContext` 前向声明（避免循环依赖，SystemContext.h 已 include 本文件）

**状态**: 已完成

### 3.2 阶段 B: FakeAxisDriver.h ✅

**文件**: `infrastructure/FakeAxisDriver.h`

**变更内容**:
1. `send()` 实现改为返回 `CommunicationResult`：
   - 未连接时返回 `Disconnected`
   - 正常时处理命令后返回 `Sent`（Fake 驱动永远通讯成功）
2. 实现 `pollFeedback(SystemContext&)`：
   - 自动推进 FakePLC::tick(10)
   - 遍历 6 个轴注入 AxisFeedback
   - 注入急停反馈 → EmergencyStopController
3. 修复成员名匹配：`enableCoupling` 替代 `couple`，`enable` 替代 `powerOn`
4. 新增 `#include <variant>` 以支持 `std::visit`

**状态**: 已完成

---

## 4. 待执行：分阶段实施计划 (阶段 C–H)

### 阶段路线图

```
阶段 A (已完成)  ISystemDriver.h — CommunicationResult + pollFeedback
阶段 B (已完成)  FakeAxisDriver.h — 适配新接口
阶段 C (待执行)  UseCaseError.h — 新增 CommunicationResult 到 variant
阶段 D (待执行)  8 个 UseCase / Orchestrator — 检查 CommunicationResult
阶段 E (待执行)  ContextRejection.h — 补充 DriverNotReady 粒度（可选）
阶段 F (待执行)  测试文件适配 — send() 返回值变更
阶段 G (待执行)  编译验证 + 全量单元测试
阶段 H (未来)    ModbusTcpAxisDriver 生产驱动实现
```

### 阶段 C: UseCaseError.h — 新增 CommunicationResult

**影响文件**: `application/UseCaseError.h`

**变更**: 在 `UseCaseError` variant 中新增 `CommunicationResult` 类型

```cpp
// 当前
using UseCaseError = std::variant<
    std::monostate,
    ContextRejection,
    RejectionReason,
    GantryRejection,
    SafetyRejection
>;

// 阶段 C 后
using UseCaseError = std::variant<
    std::monostate,           // 成功（领域规则通过 + 通讯送达）
    ContextRejection,         // 分组/轴查找失败
    RejectionReason,          // 领域规则拒绝（命令未生成）
    CommunicationResult,      // 通讯失败（命令已生成但未送达 PLC）
    GantryRejection,          // 龙门操作拒绝
    SafetyRejection           // 安全操作拒绝
>;
```

**需要新增的 include**:
```cpp
#include "infrastructure/ISystemDriver.h"  // CommunicationResult
```

### 阶段 D: 8 个调用点 — 检查 CommunicationResult

**影响文件清单**:

| # | 文件 | send() 调用行 | 返回值处理现状 |
|---|------|-------------|-------------|
| 1 | `application/axis/EnableUseCase.h` | `drv->send(AxisCommandWithId{...})` | ❌ 未检查返回值 |
| 2 | `application/axis/JogAxisUseCase.h` | `drv->send(AxisCommandWithId{...})` (×2) | ❌ 未检查 |
| 3 | `application/axis/MoveAbsoluteUseCase.h` | `drv->send(AxisCommandWithId{...})` | ❌ 未检查 |
| 4 | `application/axis/MoveRelativeUseCase.h` | `drv->send(AxisCommandWithId{...})` | ❌ 未检查 |
| 5 | `application/axis/StopAxisUseCase.h` | `drv->send(AxisCommandWithId{...})` | ❌ 未检查 |
| 6 | `application/safety/EmergencyStopUseCase.h` | `drv->send(EmergencyStopCommand{...})` | ❌ 未检查 |
| 7 | `application/safety/ReleaseEmergencyStopUseCase.h` | `drv->send(EmergencyStopCommand{...})` | ❌ 未检查 |
| 8 | `application/policy/GantryOrchestrator.h` | `drv->send(power.popPendingCommand())` 等 | ❌ 未检查 |

**统一修改模式**（以 EnableUseCase 为例）:

```cpp
// === 当前代码 (阶段 B 之后，编译会失败——send 返回 CommunicationResult 但未被使用) ===
if (axis->hasPendingCommand()) {
    if (auto* drv = group->driver()) {
        drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
    }
}
return std::monostate{};

// === 阶段 D 目标代码 ===
if (axis->hasPendingCommand()) {
    if (auto* drv = group->driver()) {
        auto commResult = drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
        if (!commResult.ok()) {
            LOG_WARN(LogLayer::APP, "EnableUC",
                "send failed for axis, status=" + std::to_string(static_cast<int>(commResult.status))
                + ", diagnostic=" + commResult.diagnostic);
            return commResult;
        }
    }
}
return std::monostate{};
```

**GantryOrchestrator 的特殊处理**:

GantryOrchestrator 调用 `drv->send()` 后有内部状态机推进（`m_step = WaitingEnabled`），需要同时：
1. 检查通讯结果
2. 若失败，将 `m_step` 推进到 `Error` 并记录 `m_lastError`

```cpp
// GantryOrchestrator::tick() — Step::EnsuringEnabled 分支
if (power.hasPendingCommand() && drv) {
    auto commResult = drv->send(power.popPendingCommand());
    if (!commResult.ok()) {
        m_step = Step::Error;
        m_lastError = commResult;
        return;
    }
}
m_step = Step::WaitingEnabled;
```

### 阶段 E: ContextRejection 粒度补充（可选）

**当前 `DriverNotReady`** 已存在于 `ContextRejection` 枚举中。当驱动指针为空（`group->driver()` 返回 nullptr）时，UseCase 可以直接返回 `ContextRejection::DriverNotReady`。

**建议**: 无需新增枚举值。当 `driver()` 返回 nullptr 或 `commResult.status == Disconnected` 时，已有充分的信息供上层决策。

### 阶段 F: 测试文件适配

#### F.1 编译期自动暴露的变更点

由于 `send()` 从 `void` 改为 `CommunicationResult`，编译器会强制要求所有调用点接受返回值。以下测试文件需要适配：

| # | 测试文件 | 适配内容 |
|---|---------|---------|
| 1 | `tests/infrastructure/test_system_integration.cpp` | `syncA()` 手动泵送改为 `driver.pollFeedback(*ctxA)` |
| 2 | `tests/application/test_enable_usecase.cpp` | UseCase 调用后检查返回值类型 |
| 3 | `tests/application/test_jog_usecase.cpp` | 同上 |
| 4 | `tests/application/test_move_absolute_usecase.cpp` | 同上 |
| 5 | `tests/application/test_move_relative_usecase.cpp` | 同上 |
| 6 | `tests/application/test_stop_usecase.cpp` | 同上 |
| 7 | `tests/application/safety/test_emergency_stop_usecase.cpp` | 同上 |
| 8 | `tests/application/policy/test_gantry_orchestrator.cpp` | 同上 |

#### F.2 test_system_integration.cpp 适配模板

**当前模式**:
```cpp
void syncA(AxisId id) {
    Axis* a = nullptr;
    ContextRejection r;
    if (ctxA->tryGetAxis(id, a, r) && a) {
        a->applyFeedback(plcA.getFeedback(id));
    }
}
```

**阶段 F 后**:
```cpp
// 在测试 fixture 中注入 FakeAxisDriver
FakeAxisDriver driverA(plcA);
ctxA->setDriver(&driverA);

// 用 driver.pollFeedback() 替代手动 syncA()
driverA.pollFeedback(*ctxA);
```

**优点**: 测试代码路径与生产代码完全一致（通过 `ISystemDriver::pollFeedback` 分发反馈）。

#### F.3 新增测试：通讯失败场景

```cpp
TEST(EnableUseCase, ReturnsCommunicationResultWhenSendFails) {
    // 模拟驱动未连接场景
    FakePLC plc;
    FakeAxisDriver driver(plc);
    driver.disconnect();  // 模拟网络断开

    SystemContext ctx;
    ctx.setDriver(&driver);

    // 需要手动注入 axis 到 ctx...（根据实际测试 fixture 调整）

    SystemManager mgr;
    // ... 设置 mgr ...

    auto error = EnableUseCase().execute(mgr, "TestGroup", AxisId::Y, true);
    ASSERT_TRUE(std::holds_alternative<CommunicationResult>(error));
    auto& cr = std::get<CommunicationResult>(error);
    EXPECT_EQ(cr.status, CommunicationResult::Status::Disconnected);
}
```

### 阶段 G: 编译验证 + 全量单元测试

**执行步骤**:

```bash
# 1. 清除 build 缓存
rm -rf build/
mkdir build && cd build

# 2. 重新 CMake 配置 + 编译
cmake .. -G "MinGW Makefiles"
cmake --build . --target all

# 3. 运行全量单元测试
ctest --output-on-failure

# 4. 检查所有测试通过
```

**预期结果**: 所有现有测试保持绿色（Fake 驱动永远通讯成功，不影响现有断言）。

**可能的问题点**:
- UseCase 的 `#include` 路径需要调整（新增 `ISystemDriver.h` 的 include）
- FakeAxisDriver 的 `handle()` 方法中的 `LOG_TRACE` 格式串可能需要调整
- `std::visit` 需要 `<variant>`（已在阶段 B 修复）

### 阶段 H: ModbusTcpAxisDriver 生产驱动（未来）

此阶段在当前重构完成后，作为独立任务进行。骨架参见 §7。

---

## 5. 各阶段详细变更清单

### 阶段 C: UseCaseError.h

**文件**: `application/UseCaseError.h`

**变更**: 单文件修改，新增 include + variant 成员

```
+ #include "infrastructure/ISystemDriver.h"

  using UseCaseError = std::variant<
      std::monostate,
      ContextRejection,
      RejectionReason,
+     CommunicationResult,
      GantryRejection,
      SafetyRejection
  >;
```

### 阶段 D: 8 个 UseCase / Orchestrator 文件

**所有文件的统一修改模式（3 行新增 + 1 行修改）**:

```diff
  if (axis->hasPendingCommand()) {
      if (auto* drv = group->driver()) {
-         drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
+         auto commResult = drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
+         if (!commResult.ok()) {
+             return commResult;
+         }
      }
  }
```

**JogAxisUseCase 有 2 个调用点**（execute 和 stop 各一个），stop 是 void 返回所以只加日志不返回错误。

**GantryOrchestrator 特殊处理**:

```diff
  if (power.hasPendingCommand() && drv) {
-     drv->send(power.popPendingCommand());
+     auto commResult = drv->send(power.popPendingCommand());
+     if (!commResult.ok()) {
+         m_step = Step::Error;
+         m_lastError = commResult;
+         return;
+     }
  }
  m_step = Step::WaitingEnabled;
```

### 阶段 F: 测试文件适配

所有测试文件的适配模式：将 `drv->send(...)` 的返回值赋给变量（如果测试不需要检查，用 `(void)` 或 `auto _ =` 抑制"返回值未使用"警告）。

**Fake 驱动永远返回 Sent**，因此现有断言完全不变。

---

## 6. 测试适配指南

### 6.1 不变项

以下测试文件**完全不受影响**：

| 测试文件 | 原因 |
|---------|------|
| `tests/domain/test_axis.cpp` | 领域层不变 |
| `tests/domain/test_system_context.cpp` | SystemContext 不变 |
| `tests/domain/gantry/test_*.cpp` | Gantry 控制器不变 |
| `tests/domain/safety/test_*.cpp` | 安全控制器不变 |
| `tests/infrastructure/test_fake_plc.cpp` | FakePLC 不变 |
| `tests/application/test_system_manager.cpp` | SystemManager 不变 |
| `tests/presentation/viewmodel/test_axis_viewmodel_core.cpp` | ViewModel 不变 |

### 6.2 需适配项（编译期强制）

所有直接或间接调用 `ISystemDriver::send()` 的测试都需要适配返回值。编译器会强制暴露所有遗漏点（`[[nodiscard]]` + 返回值类型变更）。

### 6.3 测试策略总结

| 测试层级 | 策略 |
|---------|------|
| 单元测试（领域层） | 不变 |
| 单元测试（UseCase） | 适配 send() 返回值，断言 Fake 驱动返回 Sent |
| 集成测试 | `syncA()` → `driver.pollFeedback(*ctx)` |
| 新增测试 | 通讯失败场景（Disconnected/NetworkError 等） |

---

## 7. 生产驱动骨架 (ModbusTcpAxisDriver)

### 7.1 类声明

```cpp
// infrastructure/ModbusTcpAxisDriver.h
#pragma once
#include "infrastructure/ISystemDriver.h"
#include "domain/entity/SystemContext.h"
#include <string>
#include <cstdint>

/**
 * @brief 基于 Modbus TCP 的生产驱动实现
 *
 * 命令通路:
 *   send(SystemCommand) → 翻译为 Modbus 写寄存器操作 → connect → write → read response
 *     → 成功: Sent
 *     → Socket 错误: NetworkError
 *     → 超时: Timeout
 *     → Modbus Exception: ProtocolError / Busy
 *     → 响应格式错误: InvalidResponse
 *
 * 反馈通路:
 *   pollFeedback(SystemContext) → 批量读保持寄存器/输入寄存器
 *     → 翻译为 AxisFeedback / GantryFeedback → dispatch
 *     → 读失败: 保留上次反馈值，记 WARN
 */
class ModbusTcpAxisDriver : public ISystemDriver {
public:
    /// @param host  PLC IP 地址
    /// @param port  Modbus TCP 端口（默认 502）
    /// @param timeoutMs 读写超时（毫秒）
    ModbusTcpAxisDriver(const std::string& host, uint16_t port = 502, int timeoutMs = 500);

    ~ModbusTcpAxisDriver() override;

    // ===== ISystemDriver 接口 =====

    CommunicationResult send(const SystemCommand& cmd) override;
    void pollFeedback(SystemContext& ctx) override;

    // ===== 连接管理 =====

    /// @brief 主动连接（也可在 send 时自动连接）
    bool connect();

    /// @brief 主动断开
    void disconnect();

    /// @brief 连接状态
    bool isConnected() const;

private:
    // Modbus 上下文（使用 libmodbus）
    void* m_ctx;  // modbus_t* (不在此头文件暴露 libmodbus 类型)

    std::string m_host;
    uint16_t m_port;
    int m_timeoutMs;
    bool m_connected;

    // ===== 内部翻译方法（命令方向） =====
    CommunicationResult writeSingleAxis(const AxisCommandWithId& cmd);
    CommunicationResult writeGantryCoupling(const GantryCouplingCommand& cmd);
    CommunicationResult writeGantryPower(const GantryPowerCommand& cmd);
    CommunicationResult writeEmergencyStop(const EmergencyStopCommand& cmd);

    // ===== 内部翻译方法（反馈方向） =====
    void readAxisFeedbacks(SystemContext& ctx);
    void readEmergencyStopFeedback(SystemContext& ctx);
    void readGantryFeedback(SystemContext& ctx);

    // ===== Modbus 底层 =====
    CommunicationResult writeRegister(int addr, uint16_t value);
    CommunicationResult readRegisters(int addr, int nb, uint16_t* dest);
};
```

### 7.2 send() 实现骨架

```cpp
CommunicationResult ModbusTcpAxisDriver::send(const SystemCommand& cmd) {
    if (!m_connected) {
        // 自动连接尝试
        if (!connect()) {
            return {Status::Disconnected, 0, "Not connected to " + m_host};
        }
    }

    return std::visit([this](const auto& concrete) -> CommunicationResult {
        using T = std::decay_t<decltype(concrete)>;
        if constexpr (std::is_same_v<T, AxisCommandWithId>) {
            return writeSingleAxis(concrete);
        } else if constexpr (std::is_same_v<T, GantryCouplingCommand>) {
            return writeGantryCoupling(concrete);
        } else if constexpr (std::is_same_v<T, GantryPowerCommand>) {
            return writeGantryPower(concrete);
        } else if constexpr (std::is_same_v<T, EmergencyStopCommand>) {
            return writeEmergencyStop(concrete);
        }
    }, cmd);
}

CommunicationResult ModbusTcpAxisDriver::writeRegister(int addr, uint16_t value) {
    // 1. 构造 Modbus 写请求
    int rc = modbus_write_register((modbus_t*)m_ctx, addr, value);
    if (rc == 1) {
        return {};  // Sent
    }

    // 2. 错误分析
    if (errno == ETIMEDOUT) {
        return {Status::Timeout, 0, "write register timeout, addr=" + std::to_string(addr)};
    }
    if (errno == ECONNRESET || errno == ECONNREFUSED || errno == EHOSTUNREACH) {
        m_connected = false;
        return {Status::NetworkError, 0, "socket error: " + std::string(strerror(errno))};
    }

    // 3. 检查 Modbus Exception（通过 modbus 库的错误恢复机制）
    // libmodbus 的异常响应需要特殊处理...
    // 细节留待实际实现时完善

    return {Status::NetworkError, 0, "unknown error: " + std::string(modbus_strerror(errno))};
}
```

### 7.3 错误映射表

| libmodbus 返回值/errno | CommunicationResult |
|------------------------|---------------------|
| `rc == 1` | `Sent` |
| `errno == ETIMEDOUT` | `Timeout` |
| `errno == ECONNRESET / ECONNREFUSED / EHOSTUNREACH / ENETUNREACH` | `NetworkError` |
| `errno == EMBXILFUN` (0x01) | `ProtocolError` + exceptionCode=1 |
| `errno == EMBXILADD` (0x02) | `ProtocolError` + exceptionCode=2 |
| `errno == EMBXILVAL` (0x03) | `ProtocolError` + exceptionCode=3 |
| `errno == EMBXSBUSY` (0x06) | `Busy` (注意: 不是 ProtocolError) |
| 响应长度不匹配 | `InvalidResponse` |

---

## 8. 附录：调用链全景图

### 8.1 完整的命令→反馈闭环

```
┌─────────────────────────────────────────────────────────────────────┐
│  UI (QML)                                                            │
│    ↓ 用户点击 "使能"                                                  │
│  ViewModel (QtAxisViewModel)                                         │
│    ↓                                                                │
│  UseCase::execute(manager, groupName, axisId, params)                │
│    ↓                                                                │
│  ┌───────────── 四层拦截 ─────────────┐                              │
│  │ L0: SystemManager::tryGetGroup()   │ → ContextRejection           │
│  │ L1: SystemContext::tryGetAxis()    │ → ContextRejection           │
│  │     (含安全/龙门/注册检查)          │    (安全/龙门/注册)           │
│  │ L2: Axis::enable/move/jog/...     │ → RejectionReason            │
│  │ L3: ISystemDriver::send(cmd)      │ → CommunicationResult ✅      │
│  └────────────────────────────────────┘                              │
│    ↓                                                                │
│  UseCase 返回 UseCaseError                                           │
│    ├─ monostate           → 成功 ✅                                  │
│    ├─ ContextRejection    → 分组/轴查找失败                           │
│    ├─ RejectionReason     → 领域规则拒绝                              │
│    ├─ CommunicationResult → 通讯失败 ✅ (新增)                        │
│    ├─ GantryRejection     → 龙门拒绝                                  │
│    └─ SafetyRejection     → 安全拒绝                                  │
│                                                                      │
│  ┌──────────────── 反馈通路（每 10ms） ────────────────┐              │
│  │ ISystemDriver::pollFeedback(ctx)                    │              │
│  │   ├─ Read:  从硬件读取状态                          │              │
│  │   ├─ Translate: 硬件数据 → 领域结构体               │              │
│  │   └─ Dispatch:                                     │              │
│  │       ├─ Axis::applyFeedback(fb)                   │              │
│  │       ├─ EmergencyStopController::applyFeedback()  │              │
│  │       └─ GantryCouplingController::applyFeedback() │              │
│  └────────────────────────────────────────────────────┘              │
└─────────────────────────────────────────────────────────────────────┘
```

### 8.2 分层修改影响速查表

| 层级 | 文件 | 阶段 | 修改量 | 风险 |
|------|------|------|--------|------|
| 基础设施 | `ISystemDriver.h` | A ✅ | +120 行 | 低 |
| 基础设施 | `FakeAxisDriver.h` | B ✅ | +50 行 | 低 |
| 应用层 | `UseCaseError.h` | C | +2 行 | 低 |
| 应用层/轴 | `EnableUseCase.h` | D | +3 行 | 低 |
| 应用层/轴 | `JogAxisUseCase.h` | D | +4 行 | 低 |
| 应用层/轴 | `MoveAbsoluteUseCase.h` | D | +3 行 | 低 |
| 应用层/轴 | `MoveRelativeUseCase.h` | D | +3 行 | 低 |
| 应用层/轴 | `StopAxisUseCase.h` | D | +3 行 | 低 |
| 应用层/安全 | `EmergencyStopUseCase.h` | D | +3 行 | 低 |
| 应用层/安全 | `ReleaseEmergencyStopUseCase.h` | D | +3 行 | 低 |
| 应用层/策略 | `GantryOrchestrator.h` | D | +6 行 | 中 |
| 测试层 | 8 个测试文件 | F | 小幅适配 | 低 |
| 基础设施 | `ModbusTcpAxisDriver` | H | 新文件 ~500 行 | 高 |

**总代码变更量**: 约 200 行（阶段 C–F），集中在文件末尾的 3 行模式化修改。

### 8.3 retryable() 在 UseCase 层的预期使用模式（未来增强）

当前阶段 D 中，`!commResult.ok()` 直接返回错误。未来在需要自动重试的场景（如 Jog 点动持续发送），可以扩展为：

```cpp
// 未来可能的增强（不在当前阶段 D 范围）
if (!commResult.ok()) {
    if (commResult.retryable()) {
        m_retryCount++;
        if (m_retryCount < MAX_RETRIES) {
            return std::monostate{};  // 静默重试
        }
    }
    return commResult;  // 超限或不可重试 → 报错
}
```

**注意**: 此模式不在阶段 D 实施。当前阶段只做基础的 ok() 检查，保持修改最小化。

---

## 附录：阶段执行检查清单

```
阶段 A: ISystemDriver.h
  ☑ CommunicationResult 结构体定义（7 状态 + 4 方法）
  ☑ send() 返回 CommunicationResult
  ☑ pollFeedback() 纯虚方法
  ☑ SystemContext 前向声明

阶段 B: FakeAxisDriver.h
  ☑ send() 实现返回 CommunicationResult
  ☑ pollFeedback() 实现
  ☑ handle() 方法适配新的成员名
  ☑ 新增 <variant> include

阶段 C: UseCaseError.h
  ☐ 新增 CommunicationResult 到 variant
  ☐ 新增 ISystemDriver.h include

阶段 D: 8 个 UseCase/Orchestrator
  ☐ EnableUseCase.h — 检查 commResult.ok()
  ☐ JogAxisUseCase.h — 2 处调用点
  ☐ MoveAbsoluteUseCase.h
  ☐ MoveRelativeUseCase.h
  ☐ StopAxisUseCase.h
  ☐ EmergencyStopUseCase.h
  ☐ ReleaseEmergencyStopUseCase.h
  ☐ GantryOrchestrator.h — 3 处调用点 + 状态机处理

阶段 E: ContextRejection.h (可选)
  ☐ 确认 DriverNotReady 已满足需求

阶段 F: 测试文件适配
  ☐ test_system_integration.cpp — syncA → pollFeedback
  ☐ test_enable_usecase.cpp
  ☐ test_jog_usecase.cpp
  ☐ test_move_absolute_usecase.cpp
  ☐ test_move_relative_usecase.cpp
  ☐ test_stop_usecase.cpp
  ☐ test_emergency_stop_usecase.cpp
  ☐ test_gantry_orchestrator.cpp

阶段 G: 编译验证
  ☐ cmake --build
  ☐ ctest --output-on-failure
  ☐ 全部测试绿色

阶段 H: ModbusTcpAxisDriver (未来)
  ☐ 新建 infrastructure/ModbusTcpAxisDriver.h
  ☐ 实现 send() — 完整错误映射
  ☐ 实现 pollFeedback() — 批量读 + dispatch
  ☐ 硬件联调
```

---

> **文档状态**: ✅ 设计完成，待执行阶段 C–H  
> **预计总工时**: 阶段 C–G 约 4 小时；阶段 H 约 2–3 天（含硬件调试）  
> **回滚策略**: 任一步骤可独立回滚，因为所有修改都是增量的（新增代码，非破坏性修改）
