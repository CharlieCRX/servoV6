# AxisViewModelCore 重构分析与设计

> 文档创建时间: 2026-05-17  
> 基于底层重构后的现状分析

---

## 一、当前 AxisViewModelCore 的完整问题清单

### 1.1 现状概览

```
当前签名:
AxisViewModelCore(Axis& axis, JogOrchestrator& jogOrch,
                  AutoAbsMoveOrchestrator& absOrch,
                  AutoRelMoveOrchestrator& relOrch,
                  StopAxisUseCase& stopUc)
```

问题：**这是一个"单轴单组硬绑定"的设计**——它在构造函数期就固化了轴引用和编排器引用，且完全没有 `SystemContext` / `SystemManager` / `groupName` 的概念。

---

### 1.2 问题矩阵

| # | 问题域 | 严重度 | 具体症状 |
|---|--------|--------|----------|
| 1 | **UI 无法承载多组多轴** | 🔴 致命 | 构造函数绑定单轴单组，无法像 `SystemIntegrationTest` 那样切换 `Machine_A` / `Machine_B` |
| 2 | **错误处理链路完全断裂** | 🔴 致命 | `hasError()` / `errorMessage()` 返回 `std::string`，从未被赋值；UseCase/Orchestrator 已有完整 `UseCaseError` variant，但 ViewModel 没有消费 |
| 3 | **stop() 接口与 UseCase 签名错配** | 🔴 致命 | Core 内部调用 `m_stopUc.execute(m_axis)`，但 `StopAxisUseCase::execute` 签名是 `(SystemManager&, groupName, axisId)` |
| 4 | **tick() 接口与 Orchestrator 签名错配** | 🔴 致命 | Core 调用 `m_jogOrch.update(m_axis)`，但 `JogOrchestrator` 方法是 `tick()`（无参数），且内部自行通过 manager+groupName 解析上下文 |
| 5 | **缺少 enable/disable 功能** | 🔴 致命 | 没有任何使能/掉电入口，UI 无法上电 |
| 6 | **缺少零位/相对零位操作** | 🟡 严重 | Axis 已有 `zeroAbsolutePosition()` / `setRelativeZero()` / `clearRelativeZero()`，ViewModel 无透传 |
| 7 | **缺少位置查询接口** | 🟡 严重 | Axis 有 `currentRelativePosition()` / `relativeZeroAbsolutePosition()`，ViewModel 仅暴露 `relPos()` 但无对应透传 |
| 8 | **没有 SystemContext 支持** | 🔴 致命 | 龙门控制（上下电、联动/解耦）、设备急停按钮完全没有 ViewModel 入口 |
| 9 | **没有适配 ISystemDriver** | 🟡 严重 | 当前 Core 完全不感知 `pollFeedback()`，依赖外部手动注入数据到 Axis |
| 10 | **Q_PROPERTY 暴露极度有限** | 🟡 严重 | `QtAxisViewModel` 仅暴露 `state` / `absPos` / `posLimit` / `negLimit` / `jogVelocity` / `moveVelocity`，缺少错误、使能状态、龙门状态、急停状态 |
| 11 | **hasError/errorMessage 是死代码** | 🟡 严重 | 声明但永远返回 `false` / 空字符串 |
| 12 | **硬编码 groupName "G1"** | 🟡 严重 | 日志中写死 `TraceScope("G1", "Y", traceId)`，无法适配多组 |

---

## 二、底层已具备的能力（重构目标对齐清单）

### 2.1 UseCase + Orchestrator 层已完成

| 组件 | 入口签名 | 返回类型 |
|------|----------|----------|
| `EnableUseCase` | `execute(manager, groupName, axisId, active)` | `UseCaseError` |
| `MoveAbsoluteUseCase` | `execute(manager, groupName, axisId, target)` | `UseCaseError` |
| `MoveRelativeUseCase` | `execute(manager, groupName, axisId, distance)` | `UseCaseError` |
| `JogAxisUseCase` | `execute(manager, groupName, axisId, dir)` + `stop(...)` | `UseCaseError` / `void` |
| `StopAxisUseCase` | `execute(manager, groupName, axisId)` | `UseCaseError` |
| `JogOrchestrator` | `startJog(id, dir)` / `stopJog(id, dir)` / `tick()` | 内部状态机 |
| `AutoAbsMoveOrchestrator` | `start(target)` / `tick()` | 内部状态机 |
| `AutoRelMoveOrchestrator` | `start(distance)` / `tick()` | 内部状态机 |
| `GantryOrchestrator` | `startCoupling()` / `startDecoupling()` / `tick()` | 内部状态机 |

**关键模式**：所有 Orchestrator 内部持有 `SystemManager&` + `groupName`，`tick()` 无参数。

### 2.2 SystemContext 层已完成

- `tryGetAxis(id)` — 多层拦截（安全锁定 → 龙门同步 → 龙门语义 → 容器查找）
- `gantryCouplingController()` — 联动/解耦状态机
- `gantryPowerController()` — 龙门电机使能/掉电
- `emergencyStopController()` — 急停状态机 + 命令产生/消费
- `driver()` — ISystemDriver 引用

### 2.3 UseCaseError 完整类型

```cpp
using UseCaseError = std::variant<
    std::monostate,         // 成功
    ContextRejection,       // SystemManager / SystemContext 层
    RejectionReason,        // Axis 领域层
    CommunicationResult,    // 通讯失败
    GantryRejection,        // Gantry 联动层
    SafetyRejection         // 安全域急停层
>;
```

### 2.4 ISystemDriver 通讯接口

```cpp
virtual CommunicationResult send(const SystemCommand& cmd) = 0;
virtual void pollFeedback(SystemContext& ctx) = 0;
```

---

## 三、推荐重构方案

### 3.1 核心设计原则

```
┌─────────────────────────────────────────────────────────┐
│  原则 1: ViewModel 不直接持有 Axis*                      │
│          而是通过 groupName + axisId 动态路由             │
│                                                          │
│  原则 2: ViewModel 是防腐层（Anti-Corruption Layer）      │
│          用 std::visit 将 UseCaseError → ViewModelError   │
│          UI 层永不直接消费领域枚举                        │
│                                                          │
│  原则 3: 一个 ViewModel = 一组 + 一轴                    │
│          UI 层创建 N×M 个 ViewModel 实例来控制所有组合    │
│                                                          │
│  原则 4: tick() 统一驱动                                 │
│          ViewModel::tick() → Orchestrator::tick()         │
│          SystemManager 在更上层统一做 pollFeedback()       │
└─────────────────────────────────────────────────────────┘
```

### 3.2 分层架构图

```
┌──────────────────────────────────────────────────────────┐
│  UI (QML)                                                 │
│  ├─ AxisPanel { groupName:"Machine_A", axisId: Y }       │
│  ├─ AxisPanel { groupName:"Machine_A", axisId: Z }       │
│  ├─ AxisPanel { groupName:"Machine_B", axisId: X1 }      │
│  ├─ GantryPanel { groupName:"Machine_A" }                │
│  └─ EmergencyStopButton { groupName:"Machine_A" }         │
│                                                           │
│  绑定:                                                    │
│  ├─ errorCode: string        (图标/颜色/行为分发)         │
│  ├─ errorUserMessage: string (用户可读)                   │
│  ├─ errorCategory: string    (Inline/Modal/Silent)        │
│  └─ state, absPos, relPos, enabled ...                   │
└──────────────┬───────────────────────────────────────────┘
               │ signal/property
┌──────────────▼───────────────────────────────────────────┐
│  ViewModel 层                                             │
│                                                           │
│  AxisViewModelCore (重构后)                                │
│  ├─ 构造: (SystemManager&, groupName, axisId)             │
│  ├─ 内部创建: EnableUc, MoveAbsUc, MoveRelUc, JogUc,     │
│  │           StopUc, JogOrch, AbsOrch, RelOrch            │
│  ├─ 控制: enable(), disable(), jog+/-(), moveAbs() ...   │
│  ├─ 状态: state(), absPos(), relPos(), posLimit() ...    │
│  ├─ 错误: lastError() → ViewModelError                   │
│  └─ tick(): 驱动 Orchestrator + 收集错误                  │
│                                                           │
│  GantryViewModelCore (新增)                                │
│  ├─ 构造: (SystemManager&, groupName)                     │
│  ├─ 内部持有: GantryOrchestrator                          │
│  ├─ 控制: couple(), decouple(), powerOn(), powerOff()     │
│  ├─ 状态: couplingState, powerState                       │
│  └─ tick()                                                │
│                                                           │
│  EmergencyStopViewModel (新增)                             │
│  ├─ 构造: (SystemManager&, groupName)                     │
│  ├─ 控制: emergencyStop(), releaseEmergencyStop()          │
│  ├─ 状态: isLocked, safetyState                           │
│  └─ tick()                                                │
│                                                           │
│  ViewModelError (新增结构体)                                │
│  ├─ code:        string  // "AXIS_AT_POSITIVE_LIMIT"      │
│  ├─ userMessage: string  // "轴已到达正向限位"             │
│  ├─ debugMessage:string  // "AxisId=X,pos=100.0,limit=…"  │
│  └─ category:    ErrorCategory  // Inline/Modal/Silent    │
│                                                           │
│  translate(UseCaseError) → ViewModelError (新增函数)       │
│  └─ std::visit 一次性覆盖所有 variant 分支                 │
└──────────────┬───────────────────────────────────────────┘
               │ UseCaseError / 状态读取
┌──────────────▼───────────────────────────────────────────┐
│  UseCase / Orchestrator (不变)                             │
│  └─ 继续返回 UseCaseError，不做 UI 翻译                    │
└──────────────────────────────────────────────────────────┘
```

### 3.3 重构后的 AxisViewModelCore 接口

```cpp
class AxisViewModelCore {
public:
    // ========== 构造：动态路由模式 ==========
    AxisViewModelCore(SystemManager& manager,
                      const std::string& groupName,
                      AxisId axisId);

    // ========== 1. 状态投影 (State Projection) ==========
    AxisState state() const;
    double absPos() const;
    double relPos() const;
    double relZeroAbsPos() const;        // ← 新增：相对零点绝对位置
    bool isEnabled() const;
    bool isMoving() const;               // ← 新增：是否在运动中

    // 限位
    double posLimit() const;
    double negLimit() const;
    bool atPosLimit() const;             // ← 新增：是否在正限位
    bool atNegLimit() const;             // ← 新增：是否在负限位

    // 速度
    double jogVelocity() const;
    double moveVelocity() const;

    // ========== 2. 错误状态 (防腐层翻译后) ==========
    ViewModelError lastError() const;
    bool hasError() const;
    void clearError();                   // ← 新增：确认/清除错误

    // ========== 3. 控制指令 (Control Inputs) ==========
    void enable();
    void disable();
    void jogPositivePressed();
    void jogPositiveReleased();
    void jogNegativePressed();
    void jogNegativeReleased();
    void moveAbsolute(double targetPos);
    void moveRelative(double distance);
    void stop();

    void setJogVelocity(double v);
    void setMoveVelocity(double v);

    // 零位操作 ← 新增
    void zeroAbsolutePosition();
    void setRelativeZero();
    void clearRelativeZero();

    // ========== 4. 驱动机制 (Tick) ==========
    void tick();

    // ========== 5. 元信息 ==========
    std::string groupName() const;
    AxisId axisId() const;

private:
    SystemManager& m_manager;
    std::string m_groupName;
    AxisId m_axisId;

    // 无状态 UseCase（值语义，复用）
    EnableUseCase m_enableUc;
    MoveAbsoluteUseCase m_moveAbsUc;
    MoveRelativeUseCase m_moveRelUc;
    JogAxisUseCase m_jogUc;
    StopAxisUseCase m_stopUc;

    // 有状态 Orchestrator（指针，生命周期由 ViewModel 管理）
    std::unique_ptr<JogOrchestrator> m_jogOrch;
    std::unique_ptr<AutoAbsMoveOrchestrator> m_absOrch;
    std::unique_ptr<AutoRelMoveOrchestrator> m_relOrch;

    ViewModelError m_lastError;  // 非 variant，已翻译
    bool m_hasError = false;
};
```

### 3.4 ViewModelError 结构体设计

```cpp
// presentation/viewmodel/ViewModelError.h

#pragma once
#include <string>

/**
 * @brief UI 友好的错误分类
 *
 * Inline  — 在轴控件旁内联显示（如"轴已到达正向限位"），不打断用户操作
 * Modal   — 弹窗警告（如"通讯中断"），需要用户确认
 * Silent  — 不显示，仅记录日志（如"点动停止时轴已空闲"）
 */
enum class ErrorCategory {
    Inline,
    Modal,
    Silent
};

/**
 * @brief ViewModel 层翻译后的错误结构
 *
 * 设计原则：
 *   - code:        机器可读，QML 用于图标/颜色/行为分发
 *   - userMessage: 用户可读，QML 直接绑定到 Label.text
 *   - debugMessage: 调试信息，用于日志，不暴露给最终用户
 *   - category:    决定 UI 展示策略
 */
struct ViewModelError {
    std::string code;
    std::string userMessage;
    std::string debugMessage;
    ErrorCategory category = ErrorCategory::Inline;

    bool isValid() const { return !code.empty(); }
};
```

### 3.5 UseCaseError → ViewModelError 翻译表

```cpp
// 在 AxisViewModelCore.cpp 中实现
ViewModelError AxisViewModelCore::translate(const UseCaseError& err) {
    return std::visit([](const auto& e) -> ViewModelError {
        using T = std::decay_t<decltype(e)>;

        // =============================================
        // 成功（不应被翻译，调用方先检查 monostate）
        // =============================================
        if constexpr (std::is_same_v<T, std::monostate>) {
            return {"", "", "", ErrorCategory::Silent};
        }

        // =============================================
        // Axis 领域层错误
        // =============================================
        else if constexpr (std::is_same_v<T, RejectionReason>) {
            switch (e) {
            case RejectionReason::InvalidState:
                return {"AXIS_INVALID_STATE", "轴状态无效，操作被拒绝",
                        "Axis is in an invalid state for this operation",
                        ErrorCategory::Inline};
            case RejectionReason::AlreadyMoving:
                return {"AXIS_ALREADY_MOVING", "轴正在运动中",
                        "Cannot execute operation while axis is moving",
                        ErrorCategory::Inline};
            case RejectionReason::TargetOutOfPositiveLimit:
                return {"AXIS_TARGET_OUT_OF_POS_LIMIT", "目标位置超出正向限位",
                        "Target exceeds positive soft limit",
                        ErrorCategory::Inline};
            case RejectionReason::TargetOutOfNegativeLimit:
                return {"AXIS_TARGET_OUT_OF_NEG_LIMIT", "目标位置超出负向限位",
                        "Target exceeds negative soft limit",
                        ErrorCategory::Inline};
            case RejectionReason::AtPositiveLimit:
                return {"AXIS_AT_POSITIVE_LIMIT", "轴已到达正向限位",
                        "Axis is at positive limit, forward motion blocked",
                        ErrorCategory::Inline};
            case RejectionReason::AtNegativeLimit:
                return {"AXIS_AT_NEGATIVE_LIMIT", "轴已到达负向限位",
                        "Axis is at negative limit, backward motion blocked",
                        ErrorCategory::Inline};
            case RejectionReason::InvalidArgument:
                return {"AXIS_INVALID_ARGUMENT", "参数无效",
                        "Invalid argument provided to axis operation",
                        ErrorCategory::Inline};
            case RejectionReason::UnknownError:
            case RejectionReason::None:
            default:
                return {"AXIS_UNKNOWN_ERROR", "轴发生未知错误",
                        "Unknown axis error",
                        ErrorCategory::Modal};
            }
        }

        // =============================================
        // SystemManager / SystemContext 层错误
        // =============================================
        else if constexpr (std::is_same_v<T, ContextRejection>) {
            switch (e) {
            case ContextRejection::GroupNotFound:
                return {"CTX_GROUP_NOT_FOUND", "设备分组不存在",
                        "System group not found",
                        ErrorCategory::Modal};
            case ContextRejection::GroupAlreadyExists:
                return {"CTX_GROUP_ALREADY_EXISTS", "设备分组已存在",
                        "System group already exists",
                        ErrorCategory::Modal};
            case ContextRejection::GroupNameInvalid:
                return {"CTX_GROUP_NAME_INVALID", "设备分组名称无效",
                        "System group name is invalid",
                        ErrorCategory::Modal};
            case ContextRejection::PhysicalAxisLockedByGantry:
                return {"CTX_PHYSICAL_AXIS_LOCKED", "物理轴被龙门联动锁定",
                        "Physical axis is locked by gantry coupling",
                        ErrorCategory::Inline};
            case ContextRejection::LogicalAxisUnavailableWhenDecoupled:
                return {"CTX_LOGICAL_AXIS_UNAVAILABLE", "龙门解耦时逻辑轴不可用",
                        "Logical axis unavailable when gantry is decoupled",
                        ErrorCategory::Inline};
            case ContextRejection::GantryNotSynchronized:
                return {"CTX_GANTRY_NOT_SYNCED", "龙门状态未同步",
                        "Gantry state not yet synchronized with PLC",
                        ErrorCategory::Modal};
            case ContextRejection::AxisNotRegistered:
                return {"CTX_AXIS_NOT_REGISTERED", "轴未注册",
                        "Axis not registered in system context",
                        ErrorCategory::Modal};
            case ContextRejection::SystemSafetyLocked:
                return {"CTX_SAFETY_LOCKED", "系统安全锁定中",
                        "System is in safety lock state (emergency stop active)",
                        ErrorCategory::Modal};
            case ContextRejection::DriverNotReady:
                return {"CTX_DRIVER_NOT_READY", "驱动未就绪",
                        "System driver is not ready",
                        ErrorCategory::Modal};
            case ContextRejection::None:
            default:
                return {"CTX_UNKNOWN_ERROR", "系统上下文未知错误",
                        "Unknown context error",
                        ErrorCategory::Modal};
            }
        }

        // =============================================
        // 通讯层错误
        // =============================================
        else if constexpr (std::is_same_v<T, CommunicationResult>) {
            switch (e.status) {
            case CommunicationResult::Status::NetworkError:
                return {"COMM_NETWORK_ERROR", "网络通讯故障",
                        e.diagnostic,
                        ErrorCategory::Modal};
            case CommunicationResult::Status::Timeout:
                return {"COMM_TIMEOUT", "通讯超时",
                        e.diagnostic,
                        ErrorCategory::Modal};
            case CommunicationResult::Status::Busy:
                return {"COMM_PLC_BUSY", "PLC 忙，请稍后重试",
                        e.diagnostic,
                        ErrorCategory::Inline};
            case CommunicationResult::Status::ProtocolError:
                return {"COMM_PROTOCOL_ERROR", "Modbus 协议错误",
                        "Exception code: 0x" + std::to_string(e.exceptionCode) + " " + e.diagnostic,
                        ErrorCategory::Modal};
            case CommunicationResult::Status::InvalidResponse:
                return {"COMM_INVALID_RESPONSE", "PLC 返回数据异常",
                        e.diagnostic,
                        ErrorCategory::Modal};
            case CommunicationResult::Status::Disconnected:
                return {"COMM_DISCONNECTED", "设备未连接",
                        e.diagnostic,
                        ErrorCategory::Modal};
            case CommunicationResult::Status::Sent:
            default:
                return {"COMM_UNKNOWN", "通讯未知错误",
                        e.diagnostic,
                        ErrorCategory::Modal};
            }
        }

        // =============================================
        // 龙门联动层错误
        // =============================================
        else if constexpr (std::is_same_v<T, GantryRejection>) {
            switch (e) {
            case GantryRejection::None:
                return {"GANTRY_UNKNOWN", "龙门操作未知错误",
                        "",
                        ErrorCategory::Inline};
            case GantryRejection::PowerNotEnabled:
                return {"GANTRY_POWER_NOT_ENABLED", "龙门电机未使能，请先上电",
                        "Gantry motor power not enabled before coupling/decoupling",
                        ErrorCategory::Inline};
            case GantryRejection::AlreadyInRequestedState:
                return {"GANTRY_ALREADY_IN_STATE", "龙门已在目标状态",
                        "Gantry already in the requested coupling state",
                        ErrorCategory::Silent};
            case GantryRejection::InvalidTransition:
                return {"GANTRY_INVALID_TRANSITION", "龙门状态转换非法",
                        "Gantry state transition is invalid",
                        ErrorCategory::Modal};
            case GantryRejection::CouplingInProgress:
                return {"GANTRY_COUPLING_IN_PROGRESS", "龙门联动操作进行中",
                        "Another gantry coupling operation is in progress",
                        ErrorCategory::Inline};
            default:
                return {"GANTRY_UNKNOWN", "龙门未知错误",
                        "",
                        ErrorCategory::Modal};
            }
        }

        // =============================================
        // 安全域急停层错误
        // =============================================
        else if constexpr (std::is_same_v<T, SafetyRejection>) {
            switch (e) {
            case SafetyRejection::None:
                return {"SAFETY_UNKNOWN", "安全域未知错误",
                        "",
                        ErrorCategory::Modal};
            case SafetyRejection::AlreadyStopped:
                return {"SAFETY_ALREADY_STOPPED", "系统已处于急停状态",
                        "Emergency stop already active",
                        ErrorCategory::Silent};
            case SafetyRejection::AlreadyReleased:
                return {"SAFETY_ALREADY_RELEASED", "急停已解除",
                        "Emergency stop already released",
                        ErrorCategory::Silent};
            case SafetyRejection::TransitionInProgress:
                return {"SAFETY_TRANSITION_IN_PROGRESS", "急停状态转换中",
                        "Emergency stop state transition in progress",
                        ErrorCategory::Inline};
            default:
                return {"SAFETY_UNKNOWN", "安全域未知错误",
                        "",
                        ErrorCategory::Modal};
            }
        }

        // 编译器保证所有分支覆盖
    }, err);
}
```

### 3.6 控制指令实现模式

每次 UseCase 调用后统一处理错误：

```cpp
void AxisViewModelCore::enable() {
    auto err = m_enableUc.execute(m_manager, m_groupName, m_axisId, true);
    if (!std::holds_alternative<std::monostate>(err)) {
        m_lastError = translate(err);
        m_hasError = true;
    }
}

void AxisViewModelCore::jogPositivePressed() {
    auto err = m_jogUc.execute(m_manager, m_groupName, m_axisId, Direction::Forward);
    if (!std::holds_alternative<std::monostate>(err)) {
        m_lastError = translate(err);
        m_hasError = true;
    }
}

void AxisViewModelCore::moveAbsolute(double targetPos) {
    auto err = m_moveAbsUc.execute(m_manager, m_groupName, m_axisId, targetPos);
    if (!std::holds_alternative<std::monostate>(err)) {
        m_lastError = translate(err);
        m_hasError = true;
    }
}
```

### 3.7 tick() 驱动 + 错误收集

```cpp
void AxisViewModelCore::tick() {
    // 驱动各 Orchestrator（内部从 manager + groupName 解析上下文）
    if (m_jogOrch) {
        m_jogOrch->tick();
        collectOrchError(*m_jogOrch);
    }
    if (m_absOrch) {
        m_absOrch->tick();
        collectOrchError(*m_absOrch);
    }
    if (m_relOrch) {
        m_relOrch->tick();
        collectOrchError(*m_relOrch);
    }
}

template<typename Orch>
void AxisViewModelCore::collectOrchError(Orch& orch) {
    if (orch.hasError()) {
        m_lastError = translate(orch.lastError());
        m_hasError = true;
    }
}
```

### 3.8 零位 / 相对零位操作

```cpp
void AxisViewModelCore::zeroAbsolutePosition() {
    // 通过 SystemContext 获取轴，执行领域操作
    SystemContext* group = nullptr;
    ContextRejection reason;
    if (!m_manager.tryGetGroup(m_groupName, group, reason)) {
        m_lastError = translate(UseCaseError{reason});
        m_hasError = true;
        return;
    }
    Axis* axis = nullptr;
    if (!group->tryGetAxis(m_axisId, axis, reason)) {
        m_lastError = translate(UseCaseError{reason});
        m_hasError = true;
        return;
    }
    if (!axis->zeroAbsolutePosition()) {
        m_lastError = translate(UseCaseError{axis->lastRejection()});
        m_hasError = true;
        return;
    }
    // 下发命令到驱动
    if (axis->hasPendingCommand()) {
        if (auto* drv = group->driver()) {
            auto commResult = drv->send(AxisCommandWithId{m_axisId, axis->getPendingCommand()});
            if (!commResult.ok()) {
                m_lastError = translate(UseCaseError{commResult});
                m_hasError = true;
            }
        }
    }
}

void AxisViewModelCore::setRelativeZero() { /* 类似模式 */ }
void AxisViewModelCore::clearRelativeZero() { /* 类似模式 */ }
```

> **注意**：零位/相对零位操作是否需要独立的 UseCase，还是在 ViewModel 中直接调用 Axis 领域方法 + 下发命令。取决于是否需要编排逻辑。当前这些是单步同步操作（调用 Axis 方法 + send），可以暂不创建 UseCase，在 ViewModel 中内联处理。如果后续需要错误重试/超时等编排，再抽取 UseCase。

### 3.9 GantryViewModelCore（新增）

```cpp
class GantryViewModelCore {
public:
    GantryViewModelCore(SystemManager& manager, const std::string& groupName);

    // 状态投影
    bool isCoupled() const;
    bool isDecoupled() const;
    bool isCouplingInProgress() const;
    bool isDecouplingInProgress() const;
    bool isPowerEnabled() const;
    bool isNotSynchronized() const;

    // 控制
    void powerOn();
    void powerOff();
    void couple();
    void decouple();

    // 错误
    ViewModelError lastError() const;
    bool hasError() const;

    // 驱动
    void tick();

private:
    SystemManager& m_manager;
    std::string m_groupName;
    std::unique_ptr<GantryOrchestrator> m_orch;
    ViewModelError m_lastError;
    bool m_hasError = false;
};
```

### 3.10 EmergencyStopViewModel（新增）

```cpp
class EmergencyStopViewModel {
public:
    EmergencyStopViewModel(SystemManager& manager, const std::string& groupName);

    // 状态投影
    bool isLocked() const;
    bool isTransitioning() const;
    std::string safetyStateText() const;  // "安全" / "急停中" / "解除中"

    // 控制
    void emergencyStop();
    void releaseEmergencyStop();

    // 错误
    ViewModelError lastError() const;
    bool hasError() const;

    // 驱动
    void tick();

private:
    SystemManager& m_manager;
    std::string m_groupName;
    EmergencyStopUseCase m_estopUc;
    ReleaseEmergencyStopUseCase m_releaseUc;
    ViewModelError m_lastError;
    bool m_hasError = false;
};
```

### 3.11 QtAxisViewModel 更新

```cpp
class QtAxisViewModel : public QObject {
    Q_OBJECT

    // 状态
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(double absPos READ absPos NOTIFY absPosChanged)
    Q_PROPERTY(double relPos READ relPos NOTIFY relPosChanged)
    Q_PROPERTY(bool enabled READ isEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool moving READ isMoving NOTIFY movingChanged)

    // 限位
    Q_PROPERTY(double posLimit READ posLimit NOTIFY limitsChanged)
    Q_PROPERTY(double negLimit READ negLimit NOTIFY limitsChanged)
    Q_PROPERTY(bool atPosLimit READ atPosLimit NOTIFY limitsChanged)
    Q_PROPERTY(bool atNegLimit READ atNegLimit NOTIFY limitsChanged)

    // 速度
    Q_PROPERTY(double jogVelocity READ jogVelocity WRITE setJogVelocity NOTIFY velocityChanged)
    Q_PROPERTY(double moveVelocity READ moveVelocity WRITE setMoveVelocity NOTIFY velocityChanged)

    // ★ 错误（防腐翻译后，QML 友好）
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY errorChanged)
    Q_PROPERTY(QString errorUserMessage READ errorUserMessage NOTIFY errorChanged)
    Q_PROPERTY(QString errorCategory READ errorCategory NOTIFY errorChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY errorChanged)

    // 元信息
    Q_PROPERTY(QString groupName READ groupName CONSTANT)
    Q_PROPERTY(QString axisId READ axisIdString CONSTANT)

public:
    explicit QtAxisViewModel(AxisViewModelCore* core, QObject* parent = nullptr);

    // Getters
    int state() const { return static_cast<int>(m_core->state()); }
    double absPos() const { return m_core->absPos(); }
    double relPos() const { return m_core->relPos(); }
    bool isEnabled() const { return m_core->isEnabled(); }
    bool isMoving() const { return m_core->isMoving(); }
    
    double posLimit() const { return m_core->posLimit(); }
    double negLimit() const { return m_core->negLimit(); }
    bool atPosLimit() const { return m_core->atPosLimit(); }
    bool atNegLimit() const { return m_core->atNegLimit(); }
    
    double jogVelocity() const { return m_core->jogVelocity(); }
    double moveVelocity() const { return m_core->moveVelocity(); }

    // 错误
    QString errorCode() const { return QString::fromStdString(m_core->lastError().code); }
    QString errorUserMessage() const { return QString::fromStdString(m_core->lastError().userMessage); }
    QString errorCategory() const { /* Inline/Modal/Silent */ }
    bool hasError() const { return m_core->hasError(); }

    QString groupName() const;
    QString axisIdString() const;

    // 控制
    Q_INVOKABLE void enable();
    Q_INVOKABLE void disable();
    Q_INVOKABLE void jogPositivePressed();
    Q_INVOKABLE void jogPositiveReleased();
    Q_INVOKABLE void jogNegativePressed();
    Q_INVOKABLE void jogNegativeReleased();
    Q_INVOKABLE void moveAbsolute(double targetPos);
    Q_INVOKABLE void moveRelative(double distance);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setJogVelocity(double v);
    Q_INVOKABLE void setMoveVelocity(double v);
    Q_INVOKABLE void zeroAbsolutePosition();
    Q_INVOKABLE void setRelativeZero();
    Q_INVOKABLE void clearRelativeZero();
    Q_INVOKABLE void clearError();

    void tick();

signals:
    void stateChanged();
    void absPosChanged();
    void relPosChanged();
    void enabledChanged();
    void movingChanged();
    void limitsChanged();
    void velocityChanged();
    void errorChanged();

private:
    AxisViewModelCore* m_core;
    // 缓存节流...
};
```

---

## 四、重构实施步骤

### Phase 1: 基础结构（阻塞性依赖）

| 步骤 | 内容 | 产出的文件 |
|------|------|-----------|
| 1.1 | 创建 `ViewModelError.h` 结构体 + `ErrorCategory` 枚举 | `presentation/viewmodel/ViewModelError.h` |
| 1.2 | 创建 `translate(UseCaseError) → ViewModelError` 翻译函数 | `presentation/viewmodel/ErrorTranslator.h/.cpp` |
| 1.3 | 编写 ErrorTranslator 的单元测试（覆盖所有 variant 分支） | `tests/presentation/viewmodel/test_error_translator.cpp` |

### Phase 2: 重构 AxisViewModelCore

| 步骤 | 内容 |
|------|------|
| 2.1 | 重写构造函数：`(SystemManager&, groupName, axisId)` |
| 2.2 | 内部创建所有 UseCase（值语义）+ Orchestrator（unique_ptr） |
| 2.3 | 实现 enable/disable/jog/moveabs/moverel/stop（UseCase + 错误翻译） |
| 2.4 | 实现零位操作：zeroAbsolutePosition/setRelativeZero/clearRelativeZero |
| 2.5 | 实现 tick()：驱动 Orchestrator + 收集错误 |
| 2.6 | 删除过时的 hasError/errorMessage 的 string 返回 |
| 2.7 | 新增 ViewModelError lastError() + bool hasError() + clearError() |
| 2.8 | 更新 QtAxisViewModel 的 Q_PROPERTY 映射 |
| 2.9 | 更新单元测试适配新接口 |

### Phase 3: 新增 GantryViewModelCore

| 步骤 | 内容 |
|------|------|
| 3.1 | 创建 GantryViewModelCore.h/.cpp |
| 3.2 | 创建 QtGantryViewModel（QML 适配） |
| 3.3 | 编写单元测试 |

### Phase 4: 新增 EmergencyStopViewModel

| 步骤 | 内容 |
|------|------|
| 4.1 | 创建 EmergencyStopViewModel.h/.cpp |
| 4.2 | 创建 QtEmergencyStopViewModel（QML 适配） |
| 4.3 | 编写单元测试 |

### Phase 5: SystemManager 层级 tick() 编排

| 步骤 | 内容 |
|------|------|
| 5.1 | 在 SystemManager（或上层 Loop）中统一 pollFeedback |
| 5.2 | 建立所有 ViewModel 的注册/收集机制 |
| 5.3 | 单次主循环：pollFeedback → tickAllViewModels → emit |

---

## 五、删除清单

以下现有代码将在重构中删除或大幅修改：

| 文件 | 处理方式 |
|------|----------|
| `AxisViewModelCore.h` | **重写**（新接口） |
| `AxisViewModelCore.cpp` | **重写**（新实现） |
| `QtAxisViewModel.h` | **大幅修改**（新增 Q_PROPERTY） |
| `QtAxisViewModel.cpp` | **大幅修改** |

---

## 六、关键设计决策记录

### 决策 1：为什么 ViewModel 不继承 SystemContext 而是通过 manager + groupName 动态路由？

**原因**：SystemContext 内部持有轴的状态，ViewModel 需要的是"对某个 SystemContext 中某个轴的访问能力"。通过 `(manager, groupName, axisId)` 三元组，ViewModel 是无状态的"访问器"，可以在任何时刻重新解析轴引用（例如 groupName 变化时重建 ViewModel）。

### 决策 2：为什么零位操作不使用独立 UseCase？

**原因**：零位操作是单步同步操作，不涉及多步编排或异步等待。`Axis::zeroAbsolutePosition()` 内部已完成所有领域校验，ViewModel 仅需透传 + 下发命令。如果后续需要超时重试或多步确认，再抽取为 UseCase。

### 决策 3：为什么 Orchestrator 使用 unique_ptr 而非引用？

**原因**：重构后 ViewModel 是 Orchestrator 的天然拥有者。每个 ViewModel 实例独占一组 Orchestrator，值语义（unique_ptr）更清晰地表达所有权。构造函数中动态创建，析构自动释放。

### 决策 4：为什么 ViewModelError 不用 variant？

**原因**：`ViewModelError` 的目标是 UI 友好，结构体比 variant 更适合 QML 绑定。翻译后的 `code` / `userMessage` / `category` 是固定字段，QML 可以直接绑定 `errorCode` / `errorUserMessage` 字符串属性。variant 留在 UseCase 层保持领域语义完整。

---

## 七、附录：UseCaseError 各分支 → ViewModelError 翻译速查表

| UseCaseError variant | 枚举值 | code | category |
|---------------------|--------|------|----------|
| `ContextRejection::GroupNotFound` | — | `CTX_GROUP_NOT_FOUND` | Modal |
| `ContextRejection::PhysicalAxisLockedByGantry` | — | `CTX_PHYSICAL_AXIS_LOCKED` | Inline |
| `ContextRejection::SystemSafetyLocked` | — | `CTX_SAFETY_LOCKED` | Modal |
| `RejectionReason::AtPositiveLimit` | — | `AXIS_AT_POSITIVE_LIMIT` | Inline |
| `RejectionReason::AtNegativeLimit` | — | `AXIS_AT_NEGATIVE_LIMIT` | Inline |
| `RejectionReason::TargetOutOfPositiveLimit` | — | `AXIS_TARGET_OUT_OF_POS_LIMIT` | Inline |
| `RejectionReason::InvalidState` | — | `AXIS_INVALID_STATE` | Inline |
| `RejectionReason::AlreadyMoving` | — | `AXIS_ALREADY_MOVING` | Inline |
| `CommunicationResult::Status::NetworkError` | — | `COMM_NETWORK_ERROR` | Modal |
| `CommunicationResult::Status::Timeout` | — | `COMM_TIMEOUT` | Modal |
| `CommunicationResult::Status::Busy` | — | `COMM_PLC_BUSY` | Inline |
| `GantryRejection::PowerNotEnabled` | — | `GANTRY_POWER_NOT_ENABLED` | Inline |
| `SafetyRejection::AlreadyStopped` | — | `SAFETY_ALREADY_STOPPED` | Silent |
