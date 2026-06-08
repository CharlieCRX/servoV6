# SDL3 摇杆控制完整架构设计文档

## 1. 文档概述

### 1.1 目的

本文档为 servoV6 项目设计基于 **SDL3 (Simple DirectMedia Layer 3)** 的摇杆控制完整架构方案。该方案遵循项目现有的 **Clean Architecture（整洁架构）** 分层设计，将摇杆硬件输入从物理层逐级映射到领域层的运动控制指令。

### 1.2 适用范围

- 支持多平台（Windows / Linux / Android / macOS）的物理游戏手柄/摇杆输入
- 替代 Qt QGamepad 方案（因 SDL3 提供更细粒度的设备控制、热插拔管理、力反馈等能力）
- 与现有 QML UI 的"点动/定位"双模式无缝集成

### 1.3 核心需求

| 需求编号 | 需求描述 |
|---------|---------|
| R1 | **点动模式**：摇杆推前/右 → JOG+，摇杆推后/左 → JOG-，回中 → 自动停止 |
| R2 | **定位模式**：摇杆方向键映射到定位触发按钮（前进/后退/左/右），需获取 UI 当前模式 |
| R3 | **UI 感知**：摇杆控制器需要感知 UI 层的当前模式（点动/定位）、当前选中轴、系统锁定状态 |
| R4 | **安全互斥**：摇杆与 QML 按钮共享同一套 ViewModel 接口，由底层 Orchestrator 自然互斥 |
| R5 | **跨平台**：Windows / Android 等多平台支持，通过 SDL3 统一抽象 |

---

## 2. 现有架构分析

### 2.1 分层架构速览

```
┌──────────────────────────────────────────────────────────────┐
│                    presentation (表现层)                       │
│   QML UI (ActionControlBlock.qml, MainDashboard.qml)         │
│   - currentMode: 0=点动 / 1=定位                              │
│   - currentAxis: "Y"/"Z"/"R"/"X"/"X1"/"X2"                  │
│   - systemLocked / gantryOperationLocked                     │
├──────────────────────────────────────────────────────────────┤
│   ViewModel (QtAxisViewModel / AxisViewModelCore)             │
│   - jogPositivePressed() / jogPositiveReleased()             │
│   - jogNegativePressed() / jogNegativeReleased()             │
│   - setAbsTarget() / triggerAbsMove()                        │
│   - setRelTarget() / triggerRelMove()                        │
│   - isLoading / hasBlockingError                             │
│   - tick() 驱动所有 Policy                                    │
├──────────────────────────────────────────────────────────────┤
│                    application (应用层)                        │
│   Policy / Orchestrator:                                      │
│   - JogOrchestrator   (点动编排状态机)                         │
│   - AbsMovePolicy     (绝对定位编排)                           │
│   - RelMovePolicy     (相对定位编排)                           │
│   UseCase:                                                    │
│   - JogAxisUseCase, EnableUseCase, StopAxisUseCase           │
│   - MoveAbsoluteUseCase, MoveRelativeUseCase                 │
├──────────────────────────────────────────────────────────────┤
│                      domain (领域层)                           │
│   Axis (轴聚合根) / AxisState / AxisCommand                  │
│   JogCommand / MoveCommand / StopCommand                     │
├──────────────────────────────────────────────────────────────┤
│                  infrastructure (基础设施层)                    │
│   ISystemDriver / ModbusSystemDriver / FakePLC               │
│   AsioModbusTcpClient / PlcDevice / PlcPoller               │
└──────────────────────────────────────────────────────────────┘
```

### 2.2 点动模式现有数据流

```
QML onPressed: jogPositivePressed()
    → QtAxisViewModel::jogPositivePressed()
    → AxisViewModelCore::jog(Direction::Forward)
    → JogOrchestrator::startJog(m_axisId, Direction::Forward)
    → tick() 驱动状态机:
        EnsuringEnabled → PostEnableDelay → IssuingJog → Jogging

QML onReleased: jogPositiveReleased()
    → QtAxisViewModel::jogPositiveReleased()
    → AxisViewModelCore::jogStop(Direction::Forward)
    → JogOrchestrator::stopJog(m_axisId, Direction::Forward)
    → tick() 驱动:
        IssuingStop → WaitingForIdle → PostStopDelay → EnsuringDisabled → Done
```

### 2.3 定位模式现有数据流

```
1. 用户设置目标:
   QML NumPad → setAbsTarget(value)
   → AxisViewModelCore::setAbsTarget(value)
   → Axis::setAbsTarget(value)
   → consumePendingCommands() 将 SetAbsTargetCommand 下发到 PLC

2. 用户触发执行:
   QML onClicked: triggerAbsMove()
   → AxisViewModelCore::triggerAbsMove()
   → AbsMovePolicy::startAbs(m_axisId)
   → tick() 驱动状态机
```

### 2.4 现有 QML UI 状态管理

`ActionControlBlock.qml` 维护以下关键状态：

| 属性 | 类型 | 含义 |
|------|------|------|
| `currentMode` | int | 0=点动模式, 1=定位模式 |
| `isAbsolute` | bool | 定位模式子状态: true=绝对定位, false=相对定位 |
| `currentAxis` | string | 当前选中轴名 |
| `systemLocked` | bool | 系统整体锁定（安全锁定 || 轴不可用） |
| `gantryOperationLocked` | bool | 龙门操作锁定 |
| `jogEnabled` | bool | 点动按钮可用性 |
| `isReadyForTrigger` | bool | 定位触发按钮可用性 |

---

## 3. SDL3 概述与选型理由

### 3.1 SDL3 简介

SDL3 (Simple DirectMedia Layer 3) 是跨平台的多媒体库，提供对音频、键盘、鼠标、**游戏手柄/摇杆**、触觉反馈等硬件设备的低级访问。它是 SDL2 的重大升级版本，API 更加现代化。

### 3.2 与 Qt QGamepad 的对比

| 维度 | Qt QGamepad | SDL3 |
|------|------------|------|
| 设备发现 | 依赖 Qt Gamepad 插件 | 原生 SDL 事件系统，更可靠 |
| 热插拔 | 通过 QGamepadManager 信号 | SDL 事件 `SDL_EVENT_GAMEPAD_ADDED/REMOVED` |
| 轴数量 | 标准 4 轴 (leftX/Y, rightX/Y) | 支持任意数量轴 + 触发键 |
| 死区控制 | 无内置 API，需手动处理 | 内置 `SDL_SetGamepadAxisDeadzone()` |
| 力反馈 | 不支持 | 支持（SDL Haptic API） |
| 多手柄 | 通过 deviceId 区分 | 通过 `SDL_JoystickID` / instance ID 区分 |
| 平台 | Qt 支持的所有平台 | Windows, Linux, macOS, Android, iOS |
| CMake 集成 | `find_package(Qt6)` | `find_package(SDL3)` 或 FetchContent |

### 3.3 选型理由

1. **更细粒度的设备控制**：SDL3 支持自定义死区、轴映射配置
2. **可靠的热插拔**：SDL3 的事件驱动热插拔比 QGamepad 更稳定
3. **力反馈能力**：未来可在定位到达目标时提供触觉反馈
4. **非 Qt 依赖**：SDL3 作为独立库，降低了与 Qt 版本的耦合
5. **广泛的社区支持**：SDL 是业界标准，文档和案例丰富

---

## 4. 整体架构设计

### 4.1 架构总览

在现有 Clean Architecture 基础上，新增 **摇杆控制子系统**，跨越 infrastructure → application → presentation 三层：

```
┌──────────────────────────────────────────────────────────────────┐
│                      presentation (表现层)                         │
│                                                                    │
│  ┌─────────────────────┐    ┌──────────────────────────────────┐ │
│  │ ActionControlBlock   │    │ JoystickStatusIndicator.qml     │ │
│  │ (现有，不改动)        │    │ (新增：摇杆连接状态 / 死区配置)    │ │
│  └────────┬────────────┘    └──────────────────────────────────┘ │
│           │                                                        │
│  ┌────────▼────────────────────────────────────────────────────┐ │
│  │              JoystickViewModel (新增)                         │ │
│  │   - 摇杆连接状态投影 (Q_PROPERTY)                              │ │
│  │   - 死区 / 灵敏度配置                                          │ │
│  │   - 模式感知的摇杆方向分发                                      │ │
│  │   - tick() 驱动摇杆轮询 + 状态机                               │ │
│  └────────┬────────────────────────────────────────────────────┘ │
│           │                                                        │
├───────────┼────────────────────────────────────────────────────────┤
│           │         application (应用层)                            │
│           │                                                        │
│  ┌────────▼────────────────────────────────────────────────────┐ │
│  │           JoystickControlPolicy (新增)                        │ │
│  │   - JoystickDirection → ViewModel 方法映射                    │ │
│  │   - 跨轴跳跃保护 (Cross-Axis Jump Guard)                      │ │
│  │   - 模式感知分发逻辑 (Jog vs Positioning)                     │ │
│  │   - 边沿触发 vs 电平触发切换                                   │ │
│  └────────┬────────────────────────────────────────────────────┘ │
│           │                                                        │
│  ┌────────▼────────────────────────────────────────────────────┐ │
│  │          IJoystickDriver (新增抽象接口)                       │ │
│  │   - virtual JoystickState pollState() = 0                    │ │
│  │   - virtual bool isConnected() = 0                           │ │
│  │   - virtual int deviceCount() = 0                            │ │
│  └────────┬────────────────────────────────────────────────────┘ │
│           │                                                        │
├───────────┼────────────────────────────────────────────────────────┤
│           │        infrastructure (基础设施层)                      │
│           │                                                        │
│  ┌────────▼────────────────────────────────────────────────────┐ │
│  │          SDL3JoystickDriver (新增)                            │ │
│  │   - SDL3 初始化 / 事件循环                                    │ │
│  │   - 手柄热插拔检测 (SDL_EVENT_GAMEPAD_ADDED/REMOVED)         │ │
│  │   - 轴值读取 + 死区判定                                       │ │
│  │   - 多手柄管理 (deviceId → JoystickState)                    │ │
│  │   - 力反馈接口 (预留)                                          │ │
│  └──────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
```

### 4.2 新增文件清单

| 文件 | 层 | 职责 |
|------|-----|------|
| `infrastructure/joystick/ISDLJoystickWrapper.h` | Infrastructure | SDL3 C API 的 C++ RAII 封装 |
| `infrastructure/joystick/SDL3JoystickDriver.h` | Infrastructure | `IJoystickDriver` 实现 |
| `infrastructure/joystick/SDL3JoystickDriver.cpp` | Infrastructure | SDL3 初始化 / 轮询 / 热插拔 |
| `application/joystick/IJoystickDriver.h` | Application | 摇杆驱动抽象接口 |
| `application/joystick/JoystickState.h` | Application | 摇杆状态数据结构 |
| `application/joystick/JoystickDirection.h` | Application | 方向枚举定义 |
| `application/policy/JoystickControlPolicy.h` | Application | 摇杆控制策略（模式分发/跳跃保护） |
| `presentation/viewmodel/JoystickViewModel.h` | Presentation | 摇杆 ViewModel 纯逻辑核心 |
| `presentation/viewmodel/JoystickViewModel.cpp` | Presentation | 实现 |
| `presentation/viewmodel/QtJoystickViewModel.h` | Presentation | Qt 属性绑定适配层 |
| `presentation/viewmodel/QtJoystickViewModel.cpp` | Presentation | 实现 |
| `presentation/qml/components/JoystickStatusIndicator.qml` | Presentation | 摇杆状态指示器 UI |

---

## 5. 分层设计详解

### 5.1 Infrastructure 层 — SDL3JoystickDriver

#### 5.1.1 ISDLJoystickWrapper（SDL3 C API RAII 封装）

```cpp
// infrastructure/joystick/ISDLJoystickWrapper.h
#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <functional>

/**
 * @brief SDL3 原生 API 的 C++ RAII 封装
 *
 * 职责：
 *   - SDL_Init(SDL_INIT_GAMEPAD) / SDL_Quit() 生命周期管理
 *   - 手柄打开/关闭的 RAII 封装
 *   - SDL 事件泵（内部循环或由外部驱动）
 */
class ISDLJoystickWrapper {
public:
    virtual ~ISDLJoystickWrapper() = default;

    // 初始化/关闭
    virtual bool init() = 0;
    virtual void shutdown() = 0;

    // 手柄管理
    virtual bool openGamepad(SDL_JoystickID instanceId) = 0;
    virtual void closeGamepad(SDL_JoystickID instanceId) = 0;
    virtual int  gamepadCount() const = 0;

    // 轴值读取（归一化到 [-1.0, 1.0]）
    virtual float getAxis(SDL_JoystickID instanceId, SDL_GamepadAxis axis) = 0;

    // 按钮状态
    virtual bool  getButton(SDL_JoystickID instanceId, SDL_GamepadButton button) = 0;

    // 事件轮询（驱动 SDL 内部状态机）
    virtual void  pumpEvents() = 0;

    // 死区设置
    virtual void  setAxisDeadzone(SDL_JoystickID instanceId, float deadzone) = 0;

    // 连接/断开回调
    using GamepadCallback = std::function<void(SDL_JoystickID, bool connected)>;
    virtual void setConnectionCallback(GamepadCallback cb) = 0;
};
```

#### 5.1.2 SDL3JoystickDriver

```cpp
// infrastructure/joystick/SDL3JoystickDriver.h
#pragma once
#include "application/joystick/IJoystickDriver.h"
#include "application/joystick/JoystickState.h"
#include "ISDLJoystickWrapper.h"
#include <memory>
#include <vector>
#include <map>

/**
 * @brief SDL3 摇杆驱动实现
 *
 * 实现 IJoystickDriver 接口，封装 SDL3 原生 API。
 *
 * 线程模型：单线程（由主线程 tick loop 驱动），内部无独立线程。
 * SDL 事件泵在 pollState() 中每帧调用一次。
 */
class SDL3JoystickDriver : public IJoystickDriver {
public:
    explicit SDL3JoystickDriver(std::unique_ptr<ISDLJoystickWrapper> wrapper);
    ~SDL3JoystickDriver() override;

    // IJoystickDriver 接口
    JoystickState pollState() override;
    bool isConnected() const override;
    int  deviceCount() const override;
    void setDeadzone(float deadzone) override;
    void enableHaptic(bool enable) override;   // 预留：力反馈开关

private:
    std::unique_ptr<ISDLJoystickWrapper> m_wrapper;
    bool m_connected = false;
    float m_deadzone = 0.15f;  // 默认死区 15%
    int  m_activeDeviceId = -1;

    /**
     * @brief 综合 4 轴值判定最终方向
     *
     * 优先级：leftX > leftY > rightX > rightY
     * 水平轴：正值→右(Forward), 负值→左(Backward)
     * 垂直轴：负值→前(Forward), 正值→后(Backward)  (SDL Y轴: 上推为负)
     */
    JoystickDirection computeDirection(float lx, float ly, float rx, float ry);
};
```

#### 5.1.3 SDL3JoystickDriver 核心实现伪代码

```cpp
// infrastructure/joystick/SDL3JoystickDriver.cpp (核心逻辑)

SDL3JoystickDriver::SDL3JoystickDriver(std::unique_ptr<ISDLJoystickWrapper> wrapper)
    : m_wrapper(std::move(wrapper))
{
    m_wrapper->init();
    m_wrapper->setConnectionCallback([this](SDL_JoystickID id, bool connected) {
        if (connected) {
            m_wrapper->openGamepad(id);
            m_wrapper->setAxisDeadzone(id, m_deadzone);
            m_activeDeviceId = id;
            m_connected = true;
        } else if (id == m_activeDeviceId) {
            m_wrapper->closeGamepad(id);
            m_connected = false;
            m_activeDeviceId = -1;
        }
    });
}

JoystickState SDL3JoystickDriver::pollState() {
    m_wrapper->pumpEvents();  // 驱动 SDL 事件循环（热插拔检测 + 输入更新）

    JoystickState state;
    if (!m_connected || m_activeDeviceId < 0) {
        return state;  // 返回默认状态（全部为 Neutral / 0.0）
    }

    // 读取 4 轴值（归一化 [-1.0, 1.0]）
    float lx = m_wrapper->getAxis(m_activeDeviceId, SDL_GAMEPAD_AXIS_LEFTX);
    float ly = m_wrapper->getAxis(m_activeDeviceId, SDL_GAMEPAD_AXIS_LEFTY);
    float rx = m_wrapper->getAxis(m_activeDeviceId, SDL_GAMEPAD_AXIS_RIGHTX);
    float ry = m_wrapper->getAxis(m_activeDeviceId, SDL_GAMEPAD_AXIS_RIGHTY);

    state.leftX  = lx;
    state.leftY  = ly;
    state.rightX = rx;
    state.rightY = ry;
    state.direction = computeDirection(lx, ly, rx, ry);
    state.connected = true;

    return state;
}

JoystickDirection SDL3JoystickDriver::computeDirection(
    float lx, float ly, float rx, float ry)
{
    const float dz = m_deadzone;

    // 逐个轴检查（左右摇杆独立判定，以绝对值最大的轴为准）
    struct AxisCandidate {
        float value;
        JoystickDirection positive;  // 正值方向
        JoystickDirection negative;  // 负值方向
        bool isHorizontal;           // true=水平轴, false=垂直轴
    };

    // 水平轴：正值→右(Forward), 负值→左(Backward)
    // 垂直轴：负值→前(Forward), 正值→后(Backward)
    std::vector<AxisCandidate> candidates = {
        { lx, JoystickDirection::Right, JoystickDirection::Left, true },
        { ly, JoystickDirection::Forward, JoystickDirection::Backward, false },
        { rx, JoystickDirection::Right, JoystickDirection::Left, true },
        { ry, JoystickDirection::Forward, JoystickDirection::Backward, false },
    };

    // 找出绝对值最大的非死区轴
    float maxAbs = dz;
    JoystickDirection best = JoystickDirection::Neutral;

    for (const auto& c : candidates) {
        float absVal = std::abs(c.value);
        if (absVal > maxAbs) {
            maxAbs = absVal;
            best = (c.value > 0) ? c.positive : c.negative;
        }
    }

    return best;  // Neutral if no axis exceeds deadzone
}
```

### 5.2 Application 层 — IJoystickDriver + JoystickState + JoystickControlPolicy

#### 5.2.1 抽象接口 IJoystickDriver

```cpp
// application/joystick/IJoystickDriver.h
#pragma once
#include "JoystickState.h"

/**
 * @brief 摇杆驱动抽象接口（应用层定义，基础设施层实现）
 *
 * 遵循依赖反转原则 (DIP)：
 *   - 应用层定义此接口
 *   - 基础设施层提供 SDL3JoystickDriver 实现
 *   - 测试时可以 mock 此接口
 */
class IJoystickDriver {
public:
    virtual ~IJoystickDriver() = default;

    /// @brief 读取当前摇杆状态（每 tick 调用一次）
    virtual JoystickState pollState() = 0;

    /// @brief 是否有手柄已连接
    virtual bool isConnected() const = 0;

    /// @brief 当前连接的手柄数量
    virtual int deviceCount() const = 0;

    /// @brief 设置死区值（归一化范围 [0.0, 0.5]）
    virtual void setDeadzone(float deadzone) = 0;

    /// @brief 启用/禁用触觉反馈（预留）
    virtual void enableHaptic(bool enable) = 0;
};
```

#### 5.2.2 方向枚举 JoystickDirection

```cpp
// application/joystick/JoystickDirection.h
#pragma once

/**
 * @brief 摇杆方向枚举
 *
 * 四点方向 + 回中 + 对角方向（扩展预留），
 * 用于将连续的摇杆轴值映射为离散的运动方向。
 */
enum class JoystickDirection {
    Neutral,    // 死区/回中
    Forward,    // 前 (Y轴负值)  → JOG+
    Backward,   // 后 (Y轴正值)  → JOG-
    Left,       // 左 (X轴负值)  → JOG-
    Right,      // 右 (X轴正值)  → JOG+
    // 以下为定位模式扩展预留
    ForwardLeft,
    ForwardRight,
    BackwardLeft,
    BackwardRight,
};
```

#### 5.2.3 摇杆状态数据结构 JoystickState

```cpp
// application/joystick/JoystickState.h
#pragma once
#include "JoystickDirection.h"

/**
 * @brief 摇杆完整状态快照
 *
 * 每帧由 IJoystickDriver::pollState() 返回，
 * 供上层 Policy 消费。
 */
struct JoystickState {
    bool connected = false;           // 手柄是否已连接

    // 4 轴原始值（归一化到 [-1.0, 1.0]）
    float leftX  = 0.0f;
    float leftY  = 0.0f;
    float rightX = 0.0f;
    float rightY = 0.0f;

    // 综合方向（经过死区 + 多轴优先级判定）
    JoystickDirection direction = JoystickDirection::Neutral;

    // 各轴独立方向（供细粒度控制使用）
    JoystickDirection leftXDir  = JoystickDirection::Neutral;
    JoystickDirection leftYDir  = JoystickDirection::Neutral;
    JoystickDirection rightXDir = JoystickDirection::Neutral;
    JoystickDirection rightYDir = JoystickDirection::Neutral;
};
```

#### 5.2.4 JoystickControlPolicy（核心策略）

```cpp
// application/policy/JoystickControlPolicy.h
#pragma once
#include "application/joystick/IJoystickDriver.h"
#include "application/joystick/JoystickState.h"
#include "application/joystick/JoystickDirection.h"
#include <functional>

/**
 * @brief 摇杆控制策略
 *
 * 职责：
 *   1. 跨轴跳跃保护：从 Forward 直接跳到 Backward 时，先发射 ForwardReleased
 *   2. 模式感知分发：根据 UI 的 currentMode 选择点动或定位分发策略
 *   3. 边沿/电平触发切换：定位模式使用边沿触发（避免连续重复触发）
 *   4. 多轴冲突仲裁：当多个轴同时有输入时，以绝对值最大的轴为准
 *
 * 设计原则：
 *   - 不直接依赖 Qt / QML
 *   - 通过 std::function 回调与 ViewModel 解耦
 *   - 所有状态由外部 tick() 驱动
 */
class JoystickControlPolicy {
public:
    /**
     * @brief UI 模式枚举（与 QML ActionControlBlock.currentMode 对齐）
     */
    enum class UIMode {
        Jog = 0,       // 点动模式
        Position = 1   // 定位模式
    };

    /**
     * @brief 摇杆控制动作回调类型
     *
     * 由 ViewModel 在构造时注入，Policy 不感知 QML/Qt。
     */
    struct ActionCallbacks {
        // 点动模式回调
        std::function<void()> onJogPositivePressed;
        std::function<void()> onJogPositiveReleased;
        std::function<void()> onJogNegativePressed;
        std::function<void()> onJogNegativeReleased;

        // 定位模式回调（摇杆方向 → setRelTarget + triggerRelMove）
        // distance: 正值为前进，负值为后退
        std::function<bool(double distance)> onPositionMoveRequested;

        // 定位模式回调（绝对定位 GO 按钮触发）
        std::function<void()> onAbsMoveTriggered;

        // 停止回调（紧急）
        std::function<void()> onStop;
    };

    JoystickControlPolicy() = default;

    /**
     * @brief 设置 UI 当前模式（由 QML 或 ViewModel 主动推送）
     */
    void setUIMode(UIMode mode) { m_uiMode = mode; }
    UIMode uiMode() const { return m_uiMode; }

    /**
     * @brief 设置定位步进距离（mm）
     */
    void setStepDistance(double mm) { m_stepDistance = mm; }
    double stepDistance() const { return m_stepDistance; }

    /**
     * @brief 每帧调用：读取摇杆状态，执行方向判定与分发
     *
     * @param state  当前摇杆状态（从 IJoystickDriver::pollState() 获取）
     * @param callbacks  动作回调（ViewModel 注入）
     */
    void tick(const JoystickState& state, const ActionCallbacks& callbacks);

    /// @brief 获取上一次有效方向（供调试/日志）
    JoystickDirection lastDirection() const { return m_lastDirection; }

private:
    // ── 模式与参数 ──
    UIMode m_uiMode = UIMode::Jog;
    double m_stepDistance = 1.0;  // 默认步进 1mm

    // ── 状态追踪 ──
    JoystickDirection m_lastDirection = JoystickDirection::Neutral;
    JoystickDirection m_lastJogDirection = JoystickDirection::Neutral;

    // ── 定位模式边沿触发控制 ──
    bool m_positionTriggerArmed = true;  // 回中后重置为 true

    // ── 内部方法 ──
    void dispatchJogMode(
        JoystickDirection dir,
        JoystickDirection lastDir,
        const ActionCallbacks& callbacks);

    void dispatchPositionMode(
        JoystickDirection dir,
        const ActionCallbacks& callbacks);

    /**
     * @brief 跨轴跳跃保护
     *
     * 如果从 Forward 直接跳到 Backward（不经过 Neutral），
     * 先补发上一个方向的 Released，避免电机收到冲突指令。
     */
    void applyCrossAxisGuard(
        JoystickDirection oldDir,
        JoystickDirection newDir,
        const ActionCallbacks& callbacks);
};
```

#### 5.2.5 JoystickControlPolicy::tick() 核心实现

```cpp
// application/policy/JoystickControlPolicy.cpp（核心逻辑）

void JoystickControlPolicy::tick(
    const JoystickState& state,
    const ActionCallbacks& callbacks)
{
    if (!state.connected) {
        // 手柄断开时：释放所有方向
        if (m_lastJogDirection != JoystickDirection::Neutral) {
            applyCrossAxisGuard(m_lastJogDirection,
                                JoystickDirection::Neutral, callbacks);
            m_lastJogDirection = JoystickDirection::Neutral;
        }
        m_lastDirection = JoystickDirection::Neutral;
        m_positionTriggerArmed = true;
        return;
    }

    JoystickDirection dir = state.direction;  // 综合方向

    // ═══════════════════════════════════════
    // Step 1: 跨轴跳跃保护
    // ═══════════════════════════════════════
    applyCrossAxisGuard(m_lastDirection, dir, callbacks);

    // ═══════════════════════════════════════
    // Step 2: 模式感知分发
    // ═══════════════════════════════════════
    if (m_uiMode == UIMode::Jog) {
        dispatchJogMode(dir, m_lastDirection, callbacks);
        if (dir != JoystickDirection::Neutral) {
            m_lastJogDirection = dir;
        }
    } else {
        dispatchPositionMode(dir, callbacks);
    }

    m_lastDirection = dir;
}

void JoystickControlPolicy::applyCrossAxisGuard(
    JoystickDirection oldDir,
    JoystickDirection newDir,
    const ActionCallbacks& callbacks)
{
    // 只有当旧方向是有效的运动方向时才需要补发释放
    bool oldIsForward = (oldDir == JoystickDirection::Forward ||
                         oldDir == JoystickDirection::Right);
    bool oldIsBackward = (oldDir == JoystickDirection::Backward ||
                          oldDir == JoystickDirection::Left);

    bool newIsForward = (newDir == JoystickDirection::Forward ||
                         newDir == JoystickDirection::Right);
    bool newIsBackward = (newDir == JoystickDirection::Backward ||
                          newDir == JoystickDirection::Left);

    // 从 JOG+ 直接跳到 JOG-（不经过 Neutral）
    if (oldIsForward && newIsBackward) {
        callbacks.onJogPositiveReleased();
    }
    // 从 JOG- 直接跳到 JOG+（不经过 Neutral）
    else if (oldIsBackward && newIsForward) {
        callbacks.onJogNegativeReleased();
    }
    // 从 JOG+ 回到 Neutral
    else if (oldIsForward && newDir == JoystickDirection::Neutral) {
        callbacks.onJogPositiveReleased();
    }
    // 从 JOG- 回到 Neutral
    else if (oldIsBackward && newDir == JoystickDirection::Neutral) {
        callbacks.onJogNegativeReleased();
    }
}

void JoystickControlPolicy::dispatchJogMode(
    JoystickDirection dir,
    JoystickDirection lastDir,
    const ActionCallbacks& callbacks)
{
    // 判断新方向是 JOG+ 还是 JOG-
    bool isJogPositive = (dir == JoystickDirection::Forward ||
                          dir == JoystickDirection::Right);
    bool isJogNegative = (dir == JoystickDirection::Backward ||
                          dir == JoystickDirection::Left);

    bool wasJogPositive = (lastDir == JoystickDirection::Forward ||
                           lastDir == JoystickDirection::Right);
    bool wasJogNegative = (lastDir == JoystickDirection::Backward ||
                           lastDir == JoystickDirection::Left);

    if (isJogPositive && !wasJogPositive) {
        // 新按下 JOG+
        callbacks.onJogPositivePressed();
    } else if (isJogNegative && !wasJogNegative) {
        // 新按下 JOG-
        callbacks.onJogNegativePressed();
    }
    // Neutral 已在 applyCrossAxisGuard 中处理
}

void JoystickControlPolicy::dispatchPositionMode(
    JoystickDirection dir,
    const ActionCallbacks& callbacks)
{
    // ═══════════════════════════════════════
    // 定位模式：边沿触发
    // ═══════════════════════════════════════

    if (dir == JoystickDirection::Neutral) {
        m_positionTriggerArmed = true;  // 回中 → 允许下次触发
        return;
    }

    if (!m_positionTriggerArmed) {
        return;  // 按住不放，不重复触发
    }

    m_positionTriggerArmed = false;  // 锁定，直到下次回中

    // 摇杆方向映射到相对定位距离
    double distance = 0.0;
    switch (dir) {
        case JoystickDirection::Forward:
        case JoystickDirection::Right:
            distance = +m_stepDistance;
            break;
        case JoystickDirection::Backward:
        case JoystickDirection::Left:
            distance = -m_stepDistance;
            break;
        default:
            return;  // 不处理对角方向
    }

    // 通过回调触发 setRelTarget + triggerRelMove
    callbacks.onPositionMoveRequested(distance);
}
```

### 5.3 Presentation 层 — JoystickViewModel + QtJoystickViewModel + QML

#### 5.3.1 JoystickViewModel（纯逻辑核心）

```cpp
// presentation/viewmodel/JoystickViewModel.h
#pragma once
#include "application/joystick/IJoystickDriver.h"
#include "application/policy/JoystickControlPolicy.h"
#include <memory>
#include <string>

class QtAxisViewModel;  // 前向声明

/**
 * @brief 摇杆 ViewModel 纯逻辑核心
 *
 * 职责：
 *   1. 持有 IJoystickDriver 和 JoystickControlPolicy
 *   2. 桥接 Policy 回调到目标 AxisViewModel
 *   3. tick() 驱动摇杆轮询 + Policy 分发
 *   4. 提供状态投影（连接状态、当前方向、死区等）
 */
class JoystickViewModel {
public:
    JoystickViewModel(std::unique_ptr<IJoystickDriver> driver);
    ~JoystickViewModel();

    // ── 绑定目标轴 ViewModel ──
    void bindAxisViewModel(QtAxisViewModel* axisVM);
    void unbindAxisViewModel();

    // ── UI 模式同步 ──
    void setUIMode(int mode);  // 0=点动, 1=定位
    int  uiMode() const;

    // ── 步进距离配置 ──
    void setStepDistance(double mm);
    double stepDistance() const;

    // ── 死区配置 ──
    void setDeadzone(float dz);
    float deadzone() const;

    // ── 状态投影 ──
    bool isConnected() const;
    int  deviceCount() const;
    JoystickDirection currentDirection() const;

    // ── 帧驱动（由外部定时器调用）──
    void tick();

private:
    std::unique_ptr<IJoystickDriver> m_driver;
    JoystickControlPolicy m_policy;
    QtAxisViewModel* m_boundAxisVM = nullptr;

    // ── 缓存 ──
    bool m_lastConnected = false;
    JoystickDirection m_lastDirection = JoystickDirection::Neutral;

    // ── Policy 回调绑定 ──
    JoystickControlPolicy::ActionCallbacks createCallbacks();
};
```

#### 5.3.2 JoystickViewModel 核心实现

```cpp
// presentation/viewmodel/JoystickViewModel.cpp（核心逻辑）

JoystickControlPolicy::ActionCallbacks
JoystickViewModel::createCallbacks()
{
    JoystickControlPolicy::ActionCallbacks cb;

    // ── 点动模式回调 → 透传给 AxisViewModel ──
    cb.onJogPositivePressed = [this]() {
        if (m_boundAxisVM) {
            m_boundAxisVM->jogPositivePressed();
        }
    };
    cb.onJogPositiveReleased = [this]() {
        if (m_boundAxisVM) {
            m_boundAxisVM->jogPositiveReleased();
        }
    };
    cb.onJogNegativePressed = [this]() {
        if (m_boundAxisVM) {
            m_boundAxisVM->jogNegativePressed();
        }
    };
    cb.onJogNegativeReleased = [this]() {
        if (m_boundAxisVM) {
            m_boundAxisVM->jogNegativeReleased();
        }
    };

    // ── 定位模式回调 → setRelTarget + triggerRelMove ──
    cb.onPositionMoveRequested = [this](double distance) -> bool {
        if (!m_boundAxisVM) return false;

        // ★ 安全检查：如果轴不在空闲状态，拒绝定位请求
        if (m_boundAxisVM->isLoading()) {
            return false;
        }
        if (m_boundAxisVM->hasBlockingError()) {
            return false;
        }

        // 两步操作
        bool ok = m_boundAxisVM->setRelTarget(distance);
        if (ok) {
            m_boundAxisVM->triggerRelMove();
        }
        return ok;
    };

    // ── 绝对定位 GO（预留：可映射到特定摇杆按键）──
    cb.onAbsMoveTriggered = [this]() {
        if (m_boundAxisVM) {
            m_boundAxisVM->triggerAbsMove();
        }
    };

    // ── 急停 ──
    cb.onStop = [this]() {
        if (m_boundAxisVM) {
            m_boundAxisVM->stop();
        }
    };

    return cb;
}

void JoystickViewModel::tick()
{
    // Step 1: 轮询摇杆硬件状态
    auto state = m_driver->pollState();

    // Step 2: 策略层处理（方向判定 + 跨轴保护 + 模式分发）
    m_policy.tick(state, createCallbacks());

    // Step 3: 缓存更新（供 QML 属性绑定使用）
    m_lastConnected = state.connected;
    m_lastDirection = state.direction;
}
```

#### 5.3.3 QtJoystickViewModel（Qt 属性绑定适配层）

```cpp
// presentation/viewmodel/QtJoystickViewModel.h
#pragma once
#include <QObject>
#include "JoystickViewModel.h"

/**
 * @brief 摇杆 ViewModel 的 Qt 适配层
 *
 * 将 JoystickViewModel 的属性暴露为 Q_PROPERTY，供 QML 数据绑定使用。
 */
class QtJoystickViewModel : public QObject {
    Q_OBJECT

    // ── 连接状态 ──
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY deviceCountChanged)

    // ── UI 模式 ──
    Q_PROPERTY(int uiMode READ uiMode WRITE setUIMode NOTIFY uiModeChanged)

    // ── 步进距离 ──
    Q_PROPERTY(double stepDistance READ stepDistance WRITE setStepDistance
               NOTIFY stepDistanceChanged)

    // ── 死区 ──
    Q_PROPERTY(float deadzone READ deadzone WRITE setDeadzone NOTIFY deadzoneChanged)

    // ── 当前方向（供调试/状态显示）──
    Q_PROPERTY(int currentDirection READ currentDirection NOTIFY directionChanged)
    Q_PROPERTY(QString directionText READ directionText NOTIFY directionChanged)

public:
    explicit QtJoystickViewModel(JoystickViewModel* core, QObject* parent = nullptr);

    // Getters
    bool connected() const;
    int deviceCount() const;
    int uiMode() const;
    double stepDistance() const;
    float deadzone() const;
    int currentDirection() const;
    QString directionText() const;

    // Setters
    void setUIMode(int mode);
    void setStepDistance(double mm);
    void setDeadzone(float dz);

    // ★ UI 信息注入（由 QML 层主动调用）
    Q_INVOKABLE void bindToAxis(QtAxisViewModel* axisVM);
    Q_INVOKABLE void unbindAxis();

    // 帧驱动
    void tick();

signals:
    void connectedChanged();
    void deviceCountChanged();
    void uiModeChanged();
    void stepDistanceChanged();
    void deadzoneChanged();
    void directionChanged();

private:
    JoystickViewModel* m_core;
};
```

#### 5.3.4 QML 层集成

在 `Main.qml` 中创建 `QtJoystickViewModel` 实例，并绑定到当前选中轴：

```qml
// Main.qml（新增部分）
Window {
    // ... 现有代码 ...

    // ★ 摇杆 ViewModel（通过 C++ 注册到 QML 上下文）
    QtJoystickViewModel {
        id: joystickVM
        // ★ UI 信息注入：监听当前模式和轴选择变化
        uiMode: mainDashboard.currentControlMode  // 绑定到 ActionControlBlock.currentMode
        // 通过 JS 绑定当前轴 ViewModel
        Component.onCompleted: {
            joystickVM.bindToAxis(currentViewModel);
        }
    }

    // ★ 响应轴切换和模式切换
    onCurrentAxisChanged: {
        joystickVM.bindToAxis(currentViewModel);
    }
    onCurrentGroupChanged: {
        joystickVM.bindToAxis(currentViewModel);
    }

    // ★ 弹窗打开时禁用摇杆
    property bool joystickEnabled: true
    // Dialog onOpened → joystickEnabled = false
    // Dialog onClosed → joystickEnabled = true

    // ★ 摇杆连接状态指示器（可选 UI 组件）
    JoystickStatusIndicator {
        viewModel: joystickVM
    }
}
```

---

## 6. 两种模式下的完整数据流

### 6.1 模式 A：点动模式 (UIMode::Jog)

```
物理摇杆推前 (左摇杆 Y 轴 = -0.8)
    │
    ▼
SDL3JoystickDriver::pollState()
    ├─ m_wrapper->pumpEvents()  ← SDL 事件泵
    ├─ getAxis(LEFTX/LEFTY/RIGHTX/RIGHTY)
    └─ computeDirection() → Forward (Y轴负值 = 前)
    │
    ▼
JoystickViewModel::tick()
    ├─ state = m_driver->pollState()
    └─ m_policy.tick(state, callbacks)
    │
    ▼
JoystickControlPolicy::dispatchJogMode(dir=Forward, lastDir=Neutral)
    ├─ isJogPositive = true (Forward 映射到 JOG+)
    ├─ wasJogPositive = false (上一帧是 Neutral)
    └─ callbacks.onJogPositivePressed()
    │
    ▼
JoystickViewModel::createCallbacks() 闭包
    └─ m_boundAxisVM->jogPositivePressed()
    │
    ▼
QtAxisViewModel::jogPositivePressed()
    └─ m_core->jog(Direction::Forward)
    │
    ▼
AxisViewModelCore::jog(Direction::Forward)
    └─ m_jogOrch->startJog(m_axisId, Direction::Forward)
    │
    ▼
JogOrchestrator::tick() （每 10ms）
    EnsuredEnabled → PostEnableDelay → IssuingJog → Jogging
    │
    ▼
PLC → 电机前进


════════════ 摇杆回中 ════════════

物理摇杆回中 (值 = 0.0)
    │
    ▼
SDL3JoystickDriver::pollState()
    └─ computeDirection() → Neutral (所有轴 < deadzone)
    │
    ▼
JoystickControlPolicy::applyCrossAxisGuard(old=Forward, new=Neutral)
    └─ callbacks.onJogPositiveReleased()
    │
    ▼
... → QtAxisViewModel::jogPositiveReleased()
    → AxisViewModelCore::jogStop(Direction::Forward)
    → JogOrchestrator::stopJog()
    → tick(): IssuingStop → WaitingForIdle → PostStopDelay → EnsuringDisabled → Done
    │
    ▼
PLC → 电机停止
```

### 6.2 模式 B：定位模式 (UIMode::Position)

```
UI 切换到定位模式
    │
    ▼
ActionControlBlock.currentMode = 1
    │
    ▼
QML Binding: joystickVM.uiMode = 1
    │
    ▼
QtJoystickViewModel::setUIMode(1)
    └─ m_core->setUIMode(1)
    │
    ▼
JoystickControlPolicy::setUIMode(UIMode::Position)
    m_positionTriggerArmed = true


════════════ 摇杆推前一次（边沿触发）════════════

物理摇杆推前 (值 = -0.7)  [此前在 Neutral]
    │
    ▼
JoystickControlPolicy::dispatchPositionMode(dir=Forward)
    ├─ dir != Neutral → 非回中
    ├─ m_positionTriggerArmed == true  → 可以触发
    ├─ m_positionTriggerArmed = false  → 锁定
    ├─ distance = +m_stepDistance (例如 +1.0mm)
    └─ callbacks.onPositionMoveRequested(+1.0)
    │
    ▼
JoystickViewModel 闭包:
    ├─ m_boundAxisVM->isLoading()? → 安全检查
    ├─ m_boundAxisVM->hasBlockingError()? → 安全检查
    ├─ m_boundAxisVM->setRelTarget(1.0)
    │   → AxisViewModelCore::setRelTarget(1.0)
    │   → Axis::setRelTarget(1.0)
    │   → consumePendingCommands() → SetRelTargetCommand → PLC
    └─ m_boundAxisVM->triggerRelMove()
        → AxisViewModelCore::triggerRelMove()
        → RelMovePolicy::startRel(m_axisId)
    │
    ▼
RelMovePolicy::tick() （每 10ms）
    EnsureEnabled → TriggeringMove → WaitingMotionStart → WaitingMotionFinish
    → PostStopDelay → Disabling → Done
    │
    ▼
PLC → 电机向前移动 1mm 并停止


════════════ 按住不放（不重复触发）════════════

摇杆持续推前 (值持续 = -0.7)
    │
    ▼
dispatchPositionMode(dir=Forward)
    ├─ dir != Neutral
    ├─ m_positionTriggerArmed == false  → 跳过！
    └─ return (不触发任何动作)


════════════ 回中后再推（允许再次触发）════════════

摇杆回中 (Neutral)
    └─ m_positionTriggerArmed = true  → 解锁

摇杆再推前 (Forward)
    └─ 同第一次触发流程，再移动 1mm
```

---

## 7. UI 信息获取机制

### 7.1 问题描述

摇杆定位模式需要感知以下 UI 状态：

| UI 信息 | 用途 | 获取方式 |
|---------|------|---------|
| `currentMode` (点动/定位) | 决定使用哪个分发策略 | QML Binding → `joystickVM.uiMode` |
| `currentAxis` (Y/Z/R/X/X1/X2) | 绑定到正确的 AxisViewModel | QML `onCurrentAxisChanged` → `joystickVM.bindToAxis(vm)` |
| `currentGroup` (Machine_A/B) | 同上 | QML `onCurrentGroupChanged` → `joystickVM.bindToAxis(vm)` |
| `systemLocked` | 决定是否允许操作 | 由 AxisViewModel 内部的 tryGetAxis Layer 0 安全锁自然拦截 |
| `isLoading` (Policy 运行中) | 定位模式不重复触发 | `m_boundAxisVM->isLoading()` |
| `hasBlockingError` | 阻断操作 | `m_boundAxisVM->hasBlockingError()` |
| `gantryOperationLocked` | 龙门锁定拦截 | 由 AxisViewModel 内部的 tryGetAxis 龙门语义拦截 |
| `joystickEnabled` | 弹窗禁用摇杆 | QML `joystickEnabled` 属性 → `JoystickViewModel` 暂停处理 |

### 7.2 信息流向图

```
┌─────────────────────────────────────────┐
│              QML UI Layer                │
│                                          │
│  currentMode ──────────┐                 │
│  currentAxis ──────┐   │                 │
│  currentGroup ──┐  │   │                 │
│                 │  │   │                 │
│  Binding ───────┼──┼───┼─→ joystickVM    │
│  (属性绑定)     │  │   │                 │
│                 │  │   │                 │
│  onAxisChanged ─┼──┼───┤                 │
│  onGroupChanged─┘  │   │                 │
│                    │   │                 │
│  joystickEnabled ──┘   │                 │
│   (弹窗控制)           │                 │
└────────────────────────┼─────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────┐
│        QtJoystickViewModel               │
│                                          │
│  setUIMode(mode)                         │
│  bindToAxis(axisVM)  ← Q_INVOKABLE      │
│  setEnabled(bool)                        │
│                                          │
│  tick():                                 │
│    if (!m_enabled) return;               │
│    JoystickState s = m_driver->poll();   │
│    m_policy.setUIMode(m_uiMode);         │
│    m_policy.tick(s, createCallbacks());  │
└─────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────┐
│         JoystickControlPolicy            │
│                                          │
│  m_uiMode: UIMode::Jog / UIMode::Position│
│  dispatchJogMode() / dispatchPositionMode│
│                                          │
│  通过 std::function 回调操作 AxisVM:     │
│    - jogPositivePressed()  ... 点动     │
│    - setRelTarget() + triggerRelMove()   │
└─────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────┐
│          QtAxisViewModel                 │
│                                          │
│  现有安全拦截机制（不修改）：             │
│  → tryGetAxis()                          │
│     ├─ Layer 0: 安全锁定检查             │
│     └─ Layer 1: 龙门语义检查             │
│  → JogOrchestrator                       │
│  → AbsMovePolicy / RelMovePolicy         │
└─────────────────────────────────────────┘
```

### 7.3 关键设计决策：为何不在 JoystickControlPolicy 中重复安全检查

```
摇杆 → JoystickViewModel → JoystickControlPolicy → QtAxisViewModel
                                                         │
                                                         ▼
                                                AxisViewModelCore
                                                    │
                                                    ▼
                                                tryGetAxis()
                                              (Layer 0 安全锁)
                                              (Layer 1 龙门锁)
                                                    │
                                                    ▼
                                              JogOrchestrator
                                            (AxisId+Direction 防误杀)

理由：
1. QtAxisViewModel 的方法内部已经包含完整的安全拦截链
2. 重复检查会引入双层状态管理，增加 bug 风险
3. 保持单一职责：Policy 只负责方向映射和分发，不负责安全判定
```

---

## 8. 安全与互斥设计

### 8.1 摇杆与 QML 按钮的互斥

```
         摇杆                     QML 按钮
           │                          │
           ▼                          ▼
    QtAxisViewModel::jogPositivePressed()
           │
           ▼
    AxisViewModelCore::jog(Direction::Forward)
           │
           ▼
    JogOrchestrator::startJog(axisId, Direction::Forward)
           │
           ├─ 如果已有活跃流程：
           │    ├─ 同一方向 → 幂等忽略
           │    └─ 不同方向 → stopJog() 防误杀校验通过后覆盖
           │
           └─ JogOrchestrator 内部状态机确保互斥
```

**互斥由 JogOrchestrator 内部状态机自然提供，无需额外加锁。**

### 8.2 定位模式下的互斥

| 保护机制 | 位置 | 说明 |
|---------|------|------|
| `m_positionTriggerArmed` | JoystickControlPolicy | 确保每个摇杆推-回周期只触发一次定位 |
| `isLoading()` 检查 | JoystickViewModel 回调 | Policy 运行中不触发新定位 |
| `hasBlockingError()` 检查 | JoystickViewModel 回调 | Modal 错误时阻断操作 |
| `AbsMovePolicy` / `RelMovePolicy` 自身 | Application 层 | 同一时刻只运行一个 Policy 实例（单轴单实例） |

### 8.3 安全锁定 & 龙门锁定

现有的安全机制**无需修改**：

```
JoystickControlPolicy
    → callbacks.onJogPositivePressed()
    → QtAxisViewModel::jogPositivePressed()
    → AxisViewModelCore::jog(Direction::Forward)
    → JogOrchestrator::startJog()
    → tick():
        tryGetAxis() ← Layer 0 安全锁定检查 ← EmergencyStopController
        tryGetAxis() ← Layer 1 龙门语义检查  ← GantryCouplingController
        → 若被拦截：m_step = Error, 不发送运动命令
```

### 8.4 手柄断开保护

```
SDL3JoystickDriver::pollState():
    connected = false  →

JoystickControlPolicy::tick():
    if (!state.connected) {
        applyCrossAxisGuard(lastDir, Neutral, callbacks)
        → callbacks.onJogPositiveReleased() 或 onJogNegativeReleased()
        → 确保手柄断开时电机停止
    }
```

### 8.5 弹窗禁用摇杆

沿用 servoV3 的 `JoystickControllerManager` 引用计数思想，简化为 `enabled` 属性：

```qml
// 弹窗打开时
onOpened: mainWindow.joystickEnabled = false

// 弹窗关闭时
onClosed: mainWindow.joystickEnabled = true

// JoystickViewModel 中
void tick() {
    if (!m_enabled) return;  // 弹窗打开时跳过处理
    // ...
}
```

---

## 9. SDL3 事件循环集成方案

### 9.1 线程模型

```
┌──────────────────────────────────────────────────────┐
│                    主线程 (GUI Thread)                 │
│                                                      │
│  QTimer (10ms)                                       │
│    ├─ driverA.pollFeedback(ctxA)    [Modbus 轮询]    │
│    ├─ driverB.pollFeedback(ctxB)                     │
│    ├─ allViewModels[i]->tick()      [ViewModel 驱动] │
│    ├─ emergencyVM_A/B.tick()                         │
│    ├─ gantryVM_A/B.tick()                            │
│    ├─ udpServer.tick()             [UDP 收发包]      │
│    └─ joystickVM.tick()            [SDL3 摇杆轮询]   │
│         ├─ SDL3JoystickDriver::pollState()           │
│         │    └─ m_wrapper->pumpEvents() ← SDL 事件泵  │
│         └─ JoystickControlPolicy::tick()             │
│                                                      │
│  注意: SDL_Init() / SDL_Quit() 在主线程调用           │
│        所有的 SDL API 调用都在主线程                   │
└──────────────────────────────────────────────────────┘
```

### 9.2 SDL3 初始化时机

在 `main.cpp` 中，紧接在 QML 引擎初始化后、Tick Loop 启动前：

```cpp
// main.cpp（新增部分）

// 7. SDL3 摇杆初始化
#include "infrastructure/joystick/SDL3JoystickDriver.h"
#include "presentation/viewmodel/JoystickViewModel.h"
#include "presentation/viewmodel/QtJoystickViewModel.h"

auto sdlWrapper = std::make_unique<SDL3JoystickWrapper>();  // RAII 封装
auto sdlDriver = std::make_unique<SDL3JoystickDriver>(std::move(sdlWrapper));
auto joystickCore = std::make_unique<JoystickViewModel>(std::move(sdlDriver));
QtJoystickViewModel qtJoystickVM(joystickCore.get());

engine.rootContext()->setContextProperty("joystickVM", &qtJoystickVM);

// 在 Tick Loop 中新增:
QObject::connect(&systemClock, &QTimer::timeout, [&]() {
    // ... 现有的 feedback polling / ViewModel ticks ...
    qtJoystickVM.tick();  // ★ SDL3 摇杆轮询 + 策略分发
});
```

### 9.3 CMakeLists.txt 集成

```cmake
# CMakeLists.txt（新增部分）

# ==========================================
# SDL3 集成
# ==========================================
find_package(SDL3 QUIET)

if(NOT SDL3_FOUND)
    # 通过 FetchContent 自动下载（fallback）
    include(FetchContent)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.2.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(SDL3)
    set(SDL3_LIBRARIES SDL3::SDL3)
endif()

target_link_libraries(appservoV6 PRIVATE ${SDL3_LIBRARIES})

# infrastructure/joystick 子目录
add_library(joystick_infra STATIC
    infrastructure/joystick/SDL3JoystickDriver.cpp
    infrastructure/joystick/SDL3JoystickWrapper.cpp
)
target_link_libraries(joystick_infra PUBLIC
    application    # 依赖 IJoystickDriver
    ${SDL3_LIBRARIES}
)
```

---

## 10. 测试策略

### 10.1 单元测试：JoystickControlPolicy

```cpp
// tests/application/test_joystick_control_policy.cpp

TEST(JoystickControlPolicy, JogMode_ForwardTriggersPositivePressed) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);

    int positivePressedCount = 0;
    int positiveReleasedCount = 0;
    int negativePressedCount = 0;
    int negativeReleasedCount = 0;

    JoystickControlPolicy::ActionCallbacks cb;
    cb.onJogPositivePressed  = [&]() { positivePressedCount++; };
    cb.onJogPositiveReleased = [&]() { positiveReleasedCount++; };
    cb.onJogNegativePressed  = [&]() { negativePressedCount++; };
    cb.onJogNegativeReleased = [&]() { negativeReleasedCount++; };

    // 模拟推前
    JoystickState forwardState;
    forwardState.connected = true;
    forwardState.direction = JoystickDirection::Forward;
    policy.tick(forwardState, cb);
    EXPECT_EQ(positivePressedCount, 1);
    EXPECT_EQ(negativePressedCount, 0);

    // 模拟回中
    JoystickState neutralState;
    neutralState.connected = true;
    neutralState.direction = JoystickDirection::Neutral;
    policy.tick(neutralState, cb);
    EXPECT_EQ(positiveReleasedCount, 1);
}

TEST(JoystickControlPolicy, CrossAxisJumpGuard) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);

    int positiveReleasedCount = 0;
    JoystickControlPolicy::ActionCallbacks cb;
    cb.onJogPositiveReleased = [&]() { positiveReleasedCount++; };

    // Step 1: 先推前
    JoystickState forwardState;
    forwardState.connected = true;
    forwardState.direction = JoystickDirection::Forward;
    policy.tick(forwardState, cb);

    // Step 2: 直接跳到后退（不经过 Neutral）
    JoystickState backwardState;
    backwardState.connected = true;
    backwardState.direction = JoystickDirection::Backward;
    policy.tick(backwardState, cb);

    // 跨轴保护：必须先补发 ForwardReleased
    EXPECT_GE(positiveReleasedCount, 1);
}

TEST(JoystickControlPolicy, PositionMode_EdgeTrigger) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    policy.setStepDistance(2.0);

    int moveRequestCount = 0;
    double lastDistance = 0.0;
    JoystickControlPolicy::ActionCallbacks cb;
    cb.onPositionMoveRequested = [&](double d) {
        moveRequestCount++;
        lastDistance = d;
        return true;
    };

    // 第一次推前 → 触发
    JoystickState forwardState;
    forwardState.connected = true;
    forwardState.direction = JoystickDirection::Forward;
    policy.tick(forwardState, cb);
    EXPECT_EQ(moveRequestCount, 1);
    EXPECT_EQ(lastDistance, 2.0);

    // 按住不放 → 不触发
    policy.tick(forwardState, cb);
    EXPECT_EQ(moveRequestCount, 1);  // 仍是 1

    // 回中
    JoystickState neutralState;
    neutralState.connected = true;
    neutralState.direction = JoystickDirection::Neutral;
    policy.tick(neutralState, cb);

    // 再推前 → 第二次触发
    policy.tick(forwardState, cb);
    EXPECT_EQ(moveRequestCount, 2);
}

TEST(JoystickControlPolicy, PositionMode_BackwardNegativeDistance) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    policy.setStepDistance(5.0);

    double lastDistance = 999.0;
    JoystickControlPolicy::ActionCallbacks cb;
    cb.onPositionMoveRequested = [&](double d) {
        lastDistance = d;
        return true;
    };

    JoystickState backwardState;
    backwardState.connected = true;
    backwardState.direction = JoystickDirection::Backward;
    policy.tick(backwardState, cb);

    EXPECT_EQ(lastDistance, -5.0);  // 后退 → 负距离
}
```

### 10.2 集成测试：SDL3JoystickDriver + FakeDriver

```cpp
// tests/infrastructure/test_sdl3_joystick_driver.cpp

// 使用 Mock ISDLJoystickWrapper，不依赖真实 SDL3 硬件
class MockSDLJoystickWrapper : public ISDLJoystickWrapper {
public:
    MOCK_METHOD(bool, init, (), (override));
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(bool, openGamepad, (SDL_JoystickID), (override));
    MOCK_METHOD(void, closeGamepad, (SDL_JoystickID), (override));
    MOCK_METHOD(int, gamepadCount, (), (const, override));
    MOCK_METHOD(float, getAxis, (SDL_JoystickID, SDL_GamepadAxis), (override));
    MOCK_METHOD(bool, getButton, (SDL_JoystickID, SDL_GamepadButton), (override));
    MOCK_METHOD(void, pumpEvents, (), (override));
    MOCK_METHOD(void, setAxisDeadzone, (SDL_JoystickID, float), (override));
    MOCK_METHOD(void, setConnectionCallback, (GamepadCallback), (override));
};

TEST(SDL3JoystickDriver, ComputeDirection_PrioritizesLargestAxis) {
    auto mock = std::make_unique<MockSDLJoystickWrapper>();
    // 配置 mock 行为...
    SDL3JoystickDriver driver(std::move(mock));
    // 测试方向计算逻辑
}
```

### 10.3 端到端测试：摇杆 → 电机运动

```cpp
// 使用 FakePLC + FakeAxisDriver + SDL3JoystickDriver（Mock）
// 验证从摇杆推前到电机开始运动再到摇杆回中电机停止的完整链路
```

---

## 11. 与 servoV3 方案的对比

| 特性 | servoV3 (QGamepad) | servoV6 (SDL3 方案) |
|------|-------------------|---------------------|
| 输入库 | Qt QGamepad 模块 | SDL3 |
| 模块名称 | `InputManager` / `JoystickControllerManager` | `SDL3JoystickDriver` / `JoystickControlPolicy` |
| 信号分发 | Qt Signal/Slot 直连 `MotorCtrl` | `std::function` 回调 → ViewModel → Policy |
| 点动执行 | `MotorCtrl::actionJog()` 阻塞线程 + `checkJog` 监控线程 | `JogOrchestrator` 非阻塞 tick 状态机 |
| 定位模式 | 不支持 | 摇杆方向 → `setRelTarget` + `triggerRelMove` |
| 使能/掉电 | 手动显式调用 | 编排器自动管理（`EnsuringEnabled → EnsuringDisabled`） |
| 跨轴跳跃保护 | 有（在 `InputManager::getJoystickDirection()` 中） | 有（在 `JoystickControlPolicy::applyCrossAxisGuard()` 中） |
| 死区判定 | 硬编码值 `1` | 可配置 `setDeadzone()`，SDL3 内置支持 |
| 热插拔 | `QGamepadManager` 信号 | SDL 事件 `SDL_EVENT_GAMEPAD_ADDED/REMOVED` |
| 平台限制 | 仅 Android | 全平台（Windows/Linux/macOS/Android/iOS） |
| 弹窗互斥 | 引用计数 `m_disableCount` | `enabled` 属性 |
| 力反馈 | 不支持 | 支持（SDL Haptic API，预留） |
| 测试友好度 | 依赖 Qt 事件循环，mock 困难 | 接口抽象 + 依赖注入，易于 mock |

---

## 12. 实施路线图

### Phase 1：基础设施层（预计 2-3 天）

- [ ] **1.1** 在 CMakeLists.txt 中集成 SDL3
- [ ] **1.2** 实现 `ISDLJoystickWrapper`（SDL3 C API RAII 封装）
- [ ] **1.3** 实现 `SDL3JoystickDriver`（方向计算、热插拔）
- [ ] **1.4** 编写 `MockSDLJoystickWrapper` 用于测试
- [ ] **1.5** 单元测试：`computeDirection()` 各种输入

### Phase 2：应用层（预计 1-2 天）

- [ ] **2.1** 定义 `IJoystickDriver` 抽象接口
- [ ] **2.2** 定义 `JoystickState` / `JoystickDirection` 数据结构
- [ ] **2.3** 实现 `JoystickControlPolicy`（方向分发 + 跨轴保护 + 边沿触发）
- [ ] **2.4** 单元测试：`JoystickControlPolicy` 所有状态转换

### Phase 3：表现层（预计 1-2 天）

- [ ] **3.1** 实现 `JoystickViewModel`（驱动持有 + Policy 绑定 + 回调创建）
- [ ] **3.2** 实现 `QtJoystickViewModel`（Q_PROPERTY 暴露）
- [ ] **3.3** 在 `main.cpp` 中创建并注册 `QtJoystickViewModel`
- [ ] **3.4** 在 `Main.qml` 中绑定 UI 模式 + 轴选择 + 弹窗禁用
- [ ] **3.5** 实现 `JoystickStatusIndicator.qml`（可选）

### Phase 4：集成测试（预计 1-2 天）

- [ ] **4.1** 集成测试：Mock SDL → Policy → ViewModel 完整链路
- [ ] **4.2** 端到端测试：Mock 摇杆 → FakePLC → 电机反馈闭环
- [ ] **4.3** 死区阈值调优（根据实际手柄手感）
- [ ] **4.4** 多手柄同时输入行为定义与测试

### Phase 5（后续迭代）

- [ ] **5.1** 力反馈集成（定位到达目标时震动提醒）
- [ ] **5.2** 手柄按键映射（A/B/X/Y 等按键 → 功能快捷键）
- [ ] **5.3** 摇杆灵敏度曲线配置（线性/指数）
- [ ] **5.4** 蓝牙手柄延迟补偿

---

## 13. 附录

### 13.1 关键文件索引

| 文件 | 角色 |
|------|------|
| `infrastructure/joystick/ISDLJoystickWrapper.h` | SDL3 C API 的 C++ RAII 封装接口 |
| `infrastructure/joystick/SDL3JoystickDriver.h/.cpp` | SDL3 摇杆驱动实现 |
| `application/joystick/IJoystickDriver.h` | 摇杆驱动抽象接口（应用层定义） |
| `application/joystick/JoystickState.h` | 摇杆状态数据结构 |
| `application/joystick/JoystickDirection.h` | 方向枚举 |
| `application/policy/JoystickControlPolicy.h` | 摇杆控制策略（模式分发/跳跃保护） |
| `presentation/viewmodel/JoystickViewModel.h/.cpp` | 摇杆 ViewModel 纯逻辑核心 |
| `presentation/viewmodel/QtJoystickViewModel.h/.cpp` | Qt 属性绑定适配层 |
| `presentation/qml/components/JoystickStatusIndicator.qml` | 摇杆状态指示器 UI |
| `presentation/qml/blocks/ActionControlBlock.qml` | 点动/定位 UI（现有，不改动） |
| `presentation/viewmodel/AxisViewModelCore.h/.cpp` | 轴 ViewModel 核心（现有） |
| `presentation/viewmodel/QtAxisViewModel.h/.cpp` | 轴 ViewModel Qt 适配（现有） |
| `application/policy/JogOrchestrator.h` | 点动编排器（现有，不改动） |
| `application/policy/AbsMovePolicy.h` | 绝对定位编排器（现有） |
| `application/policy/RelMovePolicy.h` | 相对定位编排器（现有） |

### 13.2 SDL3 参考资源

- SDL3 官方文档：https://wiki.libsdl.org/SDL3/FrontPage
- SDL3 Gamepad API：https://wiki.libsdl.org/SDL3/CategoryGamepad
- SDL3 事件类型：`SDL_EVENT_GAMEPAD_ADDED`, `SDL_EVENT_GAMEPAD_REMOVED`, `SDL_EVENT_GAMEPAD_AXIS_MOTION`

### 13.3 术语表

| 术语 | 含义 |
|------|------|
| DIP | 依赖反转原则 (Dependency Inversion Principle) |
| RAII | 资源获取即初始化 (Resource Acquisition Is Initialization) |
| 死区 (Deadzone) | 摇杆轴值的忽略范围，用于过滤中心抖动 |
| 边沿触发 (Edge Trigger) | 仅在状态变化时触发一次，按住不重复 |
| 电平触发 (Level Trigger) | 持续保持状态时持续触发（点动模式） |
| 跨轴跳跃保护 | 从 Forward 直接跳到 Backward 时，先补发 ForwardReleased |

---

*文档版本: v1.0*
*最后更新: 2026-06-08*
*作者: servoV6 架构组*