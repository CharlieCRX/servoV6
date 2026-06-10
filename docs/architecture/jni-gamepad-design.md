# 基于 JNI 的摇杆控制实现思路文档（修订版）

> **目标**：将 Retroid Pocket 掌机变成一个工业控制器——左手选轴，右手控轴，业务层完全不感知输入来源。

---

## 目录

1. [设计目标](#1-设计目标)
2. [架构分层总览](#2-架构分层总览)
3. [数据流向](#3-数据流向)
4. [分层详细设计](#4-分层详细设计)
   - 4.1 [基础设施层：JNI 摇杆数据捕获](#41-基础设施层jni-摇杆数据捕获)
   - 4.2 [表现层-输入：统一 InputEvent 与解释器](#42-表现层-输入统一-inputevent-与解释器)
   - 4.3 [表现层-输入：三个独立 Controller](#43-表现层-输入三个独立-controller)
   - 4.4 [应用层：InputRouter 路由](#44-应用层inputrouter-路由)
   - 4.5 [业务层：无感知复用](#45-业务层无感知复用)
5. [关键模型设计](#5-关键模型设计)
6. [模式切换设计](#6-模式切换设计)
7. [文件结构规划](#7-文件结构规划)
8. [实现路线图](#8-实现路线图)

---

## 1. 设计目标

### 1.1 用户视角（最终效果）

| 操控 | 行为 | 屏幕反馈 |
|------|------|---------|
| 左摇杆 ← / → | 切换当前轴：Y → Z → R（循环） | `当前轴：Z` |
| 右摇杆 ↑ | 当前轴正向运动（Jog Forward / RelMove +step） | 位置数值变化 |
| 右摇杆 ↓ | 当前轴负向运动 | 位置数值变化 |
| 右摇杆回中 | 停止运动 | 状态回到 Idle |
| 按钮 A | 使能 | 状态变为 Idle |
| 按钮 B | 停止 | 运动停止 |
| 按钮 X | 回零 | 位置归零 |
| 按钮 Y | 模式切换（JOG ↔ Position） | `模式：JOG` / `模式：Position` |

### 1.2 架构目标

```
                    ┌─────────────────────────────────┐
                    │        业务层 (Business)          │
                    │  ViewModel / UseCase / Orchestrator │
                    │        ↑ 完全不知道输入来源 ↑        │
                    └───────────────┬─────────────────┘
                                    │
                    ┌───────────────┴─────────────────┐
                    │      统一 InputEvent 接口         │
                    │  AxisSelectionEvent / MotionEvent │
                    └───────────────┬─────────────────┘
                                    │
            ┌───────────────────────┼───────────────────────┐
            │                       │                       │
    ┌───────┴───────┐     ┌────────┴────────┐     ┌────────┴────────┐
    │  游戏手柄      │     │   键盘 (未来)    │     │  触摸屏 (未来)   │
    │  (JNI/Android) │     │   W/A/S/D 等    │     │  QML 按钮       │
    └───────────────┘     └─────────────────┘     └─────────────────┘
```

**核心原则**：

1. **换输入设备 → 业务代码零修改**
2. **换 PLC → 输入代码零修改**
3. 所有输入设备统一转换为 `InputEvent`，业务层只消费 `InputEvent`

---

## 2. 架构分层总览

在现有 Clean Architecture 基础上，在 **presentation 层内部**新增 `input/` 子包，在 **infrastructure 层内部**新增 `joystick/` 子包：

```
┌──────────────────────────────────────────────────────────────────┐
│                      presentation (表现层)                         │
│                                                                   │
│   ┌─ QML UI (MainDashboard / ActionControl / ...) ────────────┐  │
│   ├─ viewmodel (QtAxisViewModel / AxisViewModelCore) ────────┤  │
│   │                                                            │  │
│   │  ★ 新增：presentation/input/ ★                              │  │
│   │   InputEvent.h                  统一输入事件类型定义         │  │
│   │   GamepadInputInterpreter       原始数据 → InputEvent 翻译  │  │
│   │   AxisSelectionModel            轴列表 + 当前索引管理        │  │
│   │   AxisSelectionController       左摇杆 → 选轴操作           │  │
│   │   MotionController              右摇杆 → 运动操作           │  │
│   │   ButtonController              按钮 → 使能/停止/回零/模式   │  │
│   └────────────────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────────────────┤
│                      application (应用层)                          │
│                                                                   │
│   ★ 新增：InputRouter.h (仅路由，无业务状态) ★                     │
│   UseCases / Orchestrator / Policy（完全不变）                     │
├──────────────────────────────────────────────────────────────────┤
│                        domain (领域层)                             │
│              Axis 实体 / 状态机 / 领域规则（完全不变）              │
├──────────────────────────────────────────────────────────────────┤
│                    infrastructure (基础设施层)                      │
│                                                                   │
│   ★ 新增：infrastructure/joystick/ ★                               │
│   AndroidGamepadJoystick.h/.cpp   JNI 桥接 C++ 端 (HAL 实现)      │
│                                                                   │
│   FakeAxisDriver / FakePLC / Logger / ...（不变）                  │
└──────────────────────────────────────────────────────────────────┘

平台层 (platform - Android JNI，独立于 C++ 分层):
android/src/org/qtproject/gamepad/
├── MainActivity.java             Qt Activity，安装 GamepadBridge
└── GamepadBridge.java            JNI 桥接类，MotionEvent/KeyEvent 监听
```

**依赖方向**：

```
presentation/input/
    ├── 依赖 AndroidGamepadJoystick（infrastructure 层，通过 changed() 信号）
    ├── 依赖 InputRouter（application 层，用于路由 InputEvent）
    └── 依赖 AxisViewModelCore（presentation/viewmodel 层，Controller 持有引用）

application/
    └── InputRouter 依赖 AxisViewModelCore（路由到具体 ViewModel 方法）

infrastructure/joystick/
    └── AndroidGamepadJoystick 是纯 HAL 实现，仅暴露 Q_PROPERTY + changed() 信号

application / domain 的 UseCase / Orchestrator / Axis
    └── 完全不变
```

**职责边界总览**（本次修订的核心）**：

| 组件 | 层 | 职责 | 禁止 |
|------|-----|------|------|
| `AndroidGamepadJoystick` | infrastructure/joystick | JNI 原始数据捕获，Q_PROPERTY 暴露，线程安全投递 | 禁止语义解释 |
| `InputEvent.h` | presentation/input | 定义 `AxisSelectDirection` / `MotionDirection` / `MotionEventType` / `GamepadButton` / `InputEvent` | 零外部依赖 |
| `GamepadInputInterpreter` | presentation/input | 死区 + 去抖 + 边缘触发 → emit `InputEvent` | 禁止访问 ViewModel |
| `AxisSelectionModel` | presentation/input | 管理可选轴列表 + 当前索引 | 仅数据模型，不做映射 |
| `AxisSelectionController` | presentation/input | 消费 `AxisSelect` InputEvent → 调用 `AxisSelectionModel` 或 `InputRouter` | 禁止处理 Motion/Button |
| `MotionController` | presentation/input | 消费 `Motion` InputEvent → 调用 `InputRouter` | 禁止处理 AxisSelect/Button |
| `ButtonController` | presentation/input | 消费 `Button` InputEvent → 调用 `InputRouter` | 禁止处理 AxisSelect/Motion |
| `InputRouter` | application | 仅路由：将语义方法调用透传至 ViewModel | **禁止持有任何业务状态** |

---

## 3. 数据流向

### 3.1 左摇杆选轴流

```
Android MotionEvent (AXIS_X 变化)
    │
    ▼
[JNI] GamepadBridge.nativeAxisChanged(lx, ly, rx, ry, lt, rt)
    │
    ▼
[Infrastructure/joystick] AndroidGamepadJoystick::updateAxis(lx, ...)  →  emit changed()
    │
    ▼
[Presentation/input] GamepadInputInterpreter::onGamepadChanged()
    │  读取 lx 值
    │  lx > deadzone + 300ms去抖 → emit inputEvent(AxisSelect::Right)
    │  lx < -deadzone + 300ms去抖 → emit inputEvent(AxisSelect::Left)
    │
    ▼
[Presentation/input] AxisSelectionController::onInputEvent()
    │  switch (event.type) { case AxisSelect: ... }
    │  → m_axisSelectionModel.selectRight()
    │
    ▼
[Presentation/input] AxisSelectionModel::selectRight()
    │  m_currentIndex = (m_currentIndex + 1) % m_axes.size()
    │  emit currentAxisChanged(m_axes[m_currentIndex])
    │
    ▼
[Presentation] Main.qml: currentAxis 绑定更新
    │  currentAxis 属性变更 → currentViewModel 重新绑定
    │  → InputRouter 持有的 ViewModel 引用同步切换
    │
    ▼
[Application / Presentation] AxisViewModelCore (新轴) 就绪，等待右摇杆操作
```

### 3.2 右摇杆控轴流

```
Android MotionEvent (AXIS_RY 变化)
    │
    ▼
[JNI] GamepadBridge.nativeAxisChanged(..., ry, ...)
    │
    ▼
[Infrastructure/joystick] AndroidGamepadJoystick::updateAxis(..., ry, ...)  →  emit changed()
    │
    ▼
[Presentation/input] GamepadInputInterpreter::onGamepadChanged()
    │  读取 ry 值，状态机判定：
    │
    │  ry < -deadzone && wasNeutral → emit inputEvent(Motion::Forward, Pressed)
    │  ry > +deadzone && wasNeutral → emit inputEvent(Motion::Backward, Pressed)
    │  |ry| < deadzone && wasActive  → emit inputEvent(Motion::Forward, Released) 等
    │
    ▼
[Presentation/input] MotionController::onInputEvent()
    │  switch (event.type) { case Motion: ... }
    │  → 持有 m_controlMode (JOG / Position)
    │
    │  ┌─ JOG 模式 ──────────────────────────────────────┐
    │  │ ForwardPressed  → m_inputRouter.jogForward()         │
    │  │ ForwardReleased → m_inputRouter.jogStopForward()     │
    │  │ BackwardPressed → m_inputRouter.jogBackward()        │
    │  │ BackwardReleased→ m_inputRouter.jogStopBackward()    │
    │  └─────────────────────────────────────────────────┘
    │
    │  ┌─ Position 模式 ─────────────────────────────────┐
    │  │ ForwardPressed  → m_inputRouter.relMove(+step)      │
    │  │ BackwardPressed → m_inputRouter.relMove(-step)     │
    │  │ (Released 无操作)                                   │
    │  └─────────────────────────────────────────────────┘
    │
    ▼
[Application] InputRouter::jogForward()
    │  if (m_currentViewModel) m_currentViewModel->jog(Direction::Forward);
    │  不做任何业务判断，仅透传
    │
    ▼
[Application] AxisViewModelCore.jog(Direction::Forward)
    │  → JogOrchestrator → Axis 领域层 → IAxisDriver.send()
    │
    ▼
[Infrastructure] FakeAxisDriver → FakePLC（或真实 PLC）
```

### 3.3 按钮流

```
Android KeyEvent (keyCode=96/97/99/100)
    │
    ▼
[JNI] GamepadBridge.nativeButtonChanged(keyCode, pressed)
    │
    ▼
[Infrastructure/joystick] AndroidGamepadJoystick::updateButton(...)  →  emit changed()
    │
    ▼
[Presentation/input] GamepadInputInterpreter::onGamepadChanged()
    │  边缘检测（按下/释放）
    │  A pressed → emit inputEvent(Button::A, pressed=true)
    │  B pressed → emit inputEvent(Button::B, pressed=true)
    │  X pressed → emit inputEvent(Button::X, pressed=true)
    │  Y pressed → emit inputEvent(Button::Y, pressed=true)
    │
    ▼
[Presentation/input] ButtonController::onInputEvent()
    │  switch (event.type) { case Button: ... }
    │  A → m_inputRouter.enable()
    │  B → m_inputRouter.stop()
    │  X → m_inputRouter.zeroAbsolute()
    │  Y → MotionController::toggleMode()（委托给 MotionController 切换模式）
    │
    ▼
[Application] InputRouter::enable() / stop() / zeroAbsolute()
    │  if (m_currentViewModel) m_currentViewModel->enable(true) / stop() / zeroAbsolutePosition();
    │
    ▼
[Application / Presentation] 状态变更反映到 UI
```

---

## 4. 分层详细设计

### 4.1 基础设施层：JNI 摇杆数据捕获

#### 4.1.1 文件位置

```
android/src/org/qtproject/gamepad/
├── MainActivity.java                 # Qt Activity，安装 GamepadBridge
└── GamepadBridge.java                 # JNI 桥接类，监听 MotionEvent / KeyEvent

infrastructure/joystick/
├── AndroidGamepadJoystick.h           # C++ HAL 单例，暴露 Q_PROPERTY
└── AndroidGamepadJoystick.cpp         # JNI 实现 + updateAxis/updateButton
```

#### 4.1.2 MainActivity.java

已设计完成，核心逻辑不变。

#### 4.1.3 GamepadBridge.java

已设计完成，核心逻辑不变。

#### 4.1.4 AndroidGamepadJoystick (C++) — 重命名为基础设施层 HAL 组件

**设计调整**：

- ~~`AndroidGamepad`~~ → **`AndroidGamepadJoystick`**
- 放置在 `infrastructure/joystick/` 而非 `input/`
- 职责：纯硬件抽象层（HAL），仅负责 JNI 数据捕获 + Q_PROPERTY 暴露 + 线程安全投递
- 不包含任何语义解释（死区、去抖、状态机等逻辑由上层 `GamepadInputInterpreter` 负责）

```cpp
// infrastructure/joystick/AndroidGamepadJoystick.h
#pragma once

#include <QObject>

/// @brief 硬件抽象层：Android 游戏手柄原始数据捕获
/// 职责仅为 JNI 回调 → C++ 数据投递，不包含任何语义解释
class AndroidGamepadJoystick : public QObject
{
    Q_OBJECT

    Q_PROPERTY(float lx READ lx NOTIFY changed)
    Q_PROPERTY(float ly READ ly NOTIFY changed)
    Q_PROPERTY(float rx READ rx NOTIFY changed)
    Q_PROPERTY(float ry READ ry NOTIFY changed)
    Q_PROPERTY(float lt READ lt NOTIFY changed)
    Q_PROPERTY(float rt READ rt NOTIFY changed)
    Q_PROPERTY(bool a READ a NOTIFY changed)
    Q_PROPERTY(bool b READ b NOTIFY changed)
    Q_PROPERTY(bool x READ x NOTIFY changed)
    Q_PROPERTY(bool y READ y NOTIFY changed)

public:
    static AndroidGamepadJoystick &instance();

    // Getters
    float lx() const { return m_lx; }
    float ly() const { return m_ly; }
    float rx() const { return m_rx; }
    float ry() const { return m_ry; }
    float lt() const { return m_lt; }
    float rt() const { return m_rt; }
    bool a() const { return m_a; }
    bool b() const { return m_b; }
    bool x() const { return m_x; }
    bool y() const { return m_y; }

    // 仅供 JNI 调用（内部使用）
    void updateAxis(float lx, float ly, float rx, float ry, float lt, float rt);
    void updateButton(int keyCode, bool pressed);

signals:
    /// @brief 任何轴值或按钮状态变化时发出
    void changed();

private:
    AndroidGamepadJoystick() = default;

    float m_lx = 0;
    float m_ly = 0;
    float m_rx = 0;
    float m_ry = 0;
    float m_lt = 0;
    float m_rt = 0;
    bool m_a = false;
    bool m_b = false;
    bool m_x = false;
    bool m_y = false;
};
```

```cpp
// infrastructure/joystick/AndroidGamepadJoystick.cpp
#include "AndroidGamepadJoystick.h"
#include <QMetaObject>

AndroidGamepadJoystick &AndroidGamepadJoystick::instance()
{
    static AndroidGamepadJoystick g;
    return g;
}

void AndroidGamepadJoystick::updateAxis(float lx, float ly, float rx, float ry, float lt, float rt)
{
    m_lx = lx;
    m_ly = ly;
    m_rx = rx;
    m_ry = ry;
    m_lt = lt;
    m_rt = rt;
    emit changed();
}

void AndroidGamepadJoystick::updateButton(int keyCode, bool pressed)
{
    switch (keyCode) {
    case 96:  m_a = pressed; break;
    case 97:  m_b = pressed; break;
    case 99:  m_x = pressed; break;
    case 100: m_y = pressed; break;
    default:  break;
    }
    emit changed();
}

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <jni.h>

extern "C" JNIEXPORT void JNICALL
Java_org_qtproject_gamepad_GamepadBridge_nativeAxisChanged(
    JNIEnv *, jclass,
    jfloat lx, jfloat ly, jfloat rx, jfloat ry, jfloat lt, jfloat rt)
{
    auto &pad = AndroidGamepadJoystick::instance();
    QMetaObject::invokeMethod(
        &pad,
        [&]() { pad.updateAxis(lx, ly, rx, ry, lt, rt); },
        Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL
Java_org_qtproject_gamepad_GamepadBridge_nativeButtonChanged(
    JNIEnv *, jclass, jint keyCode, jboolean pressed)
{
    auto &pad = AndroidGamepadJoystick::instance();
    QMetaObject::invokeMethod(
        &pad,
        [&]() { pad.updateButton(keyCode, pressed); },
        Qt::QueuedConnection);
}
#endif // Q_OS_ANDROID
```

---

### 4.2 表现层-输入：统一 InputEvent 与解释器

**位置**：`presentation/input/`（Input 本质是用户输入，属于 Presentation 层的一部分）

#### 4.2.1 InputEvent 类型定义（零外部依赖）

```cpp
// presentation/input/InputEvent.h
#pragma once

/// @brief 左摇杆产生的轴选择事件
enum class AxisSelectDirection {
    Left,    // 左摇杆 ←
    Right    // 左摇杆 →
};

/// @brief 右摇杆产生的运动事件
enum class MotionDirection {
    Forward,   // 右摇杆 ↑
    Backward   // 右摇杆 ↓
};

enum class MotionEventType {
    Pressed,
    Released
};

/// @brief 按钮事件
enum class GamepadButton {
    A,  // 使能
    B,  // 停止
    X,  // 回零
    Y   // 模式切换
};

/// @brief 统一的摇杆输入事件（替代原始的 float lx/ly/rx/ry）
/// 业务层只消费这些事件，不知道事件来自摇杆、键盘还是触摸屏
struct InputEvent {
    enum class Type {
        None,
        AxisSelect,        // 左摇杆选轴
        Motion,            // 右摇杆运动
        Button,            // 按钮
    };

    Type type = Type::None;

    // AxisSelect 字段
    AxisSelectDirection axisDir = AxisSelectDirection::Right;

    // Motion 字段
    MotionDirection motionDir = MotionDirection::Forward;
    MotionEventType motionType = MotionEventType::Pressed;

    // Button 字段
    GamepadButton button = GamepadButton::A;
    bool buttonPressed = false;  // true=按下, false=释放
};
```

#### 4.2.2 GamepadInputInterpreter

**职责**：将 `AndroidGamepadJoystick` 的原始轴值/按钮状态 **翻译** 为统一的 `InputEvent`。

**设计要点**：

| 要点 | 说明 |
|------|------|
| **死区（Deadzone）** | `|lx| < 0.3` 视为中立，避免摇杆物理回中偏差导致误触发 |
| **去抖（Debounce）** | 左摇杆选轴：连续拨动需间隔 300ms 才触发下一次切换 |
| **边缘触发** | 右摇杆：从中立进入非中立 → Pressed；从非中立回到中立 → Released |
| **防重复** | 同一方向不重复 emit Pressed（状态机记录 `m_lastRYDirection`）|

**接口草稿**：

```cpp
// presentation/input/GamepadInputInterpreter.h
#pragma once

#include <QObject>
#include "InputEvent.h"

class AndroidGamepadJoystick;  // forward-declare from infrastructure/joystick/

/// @brief 将 HAL 层原始摇杆数据翻译为统一的 InputEvent
/// 负责：死区判断 / 去抖 / 边缘触发状态机
class GamepadInputInterpreter : public QObject
{
    Q_OBJECT

public:
    explicit GamepadInputInterpreter(QObject* parent = nullptr);

    /// @brief 启动解释器，连接 AndroidGamepadJoystick::changed() 信号
    void start();

signals:
    /// @brief 当有新的输入事件时发出（统一事件总线）
    void inputEvent(const InputEvent& event);

private slots:
    void onGamepadChanged();

private:
    // ── 左摇杆状态 ──
    float m_lastLX = 0.0f;
    qint64 m_lastAxisSelectTime = 0;
    static constexpr float kDeadzone = 0.3f;
    static constexpr int kAxisSelectDebounceMs = 300;

    // ── 右摇杆状态 ──
    enum class RYDirection { Neutral, Forward, Backward };
    RYDirection m_lastRYDir = RYDirection::Neutral;

    // ── 按钮状态（用于边缘检测）──
    bool m_lastA = false;
    bool m_lastB = false;
    bool m_lastX = false;
    bool m_lastY = false;
};
```

#### 4.2.3 AxisSelectionModel

**职责**：管理当前选中轴的索引，响应左摇杆选轴事件。

```cpp
// presentation/input/AxisSelectionModel.h
#pragma once

#include <QObject>
#include <vector>
#include "domain/entity/AxisId.h"

/// @brief 纯数据模型：管理可选轴列表 + 当前选中索引
/// 不包含任何 InputEvent 消费逻辑（由 AxisSelectionController 负责）
class AxisSelectionModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentAxisName READ currentAxisName NOTIFY currentAxisChanged)

public:
    explicit AxisSelectionModel(QObject* parent = nullptr);

    /// @brief 初始化可选轴列表（默认 Y / Z / R）
    void setAxes(const std::vector<AxisId>& axes);

    /// @brief 左摇杆 ←：切换到上一个轴（循环）
    void selectLeft();

    /// @brief 左摇杆 →：切换到下一个轴（循环）
    void selectRight();

    /// @brief 直接设置当前轴（供 QML UI 点击时调用）
    void setCurrentAxis(AxisId id);

    AxisId currentAxis() const { return m_axes[m_currentIndex]; }
    QString currentAxisName() const;

signals:
    void currentAxisChanged(AxisId id);

private:
    std::vector<AxisId> m_axes = { AxisId::Y, AxisId::Z, AxisId::R };
    int m_currentIndex = 0;
};
```

---

### 4.3 表现层-输入：三个独立 Controller

**设计核心**：将原 `InputToBusinessMapper` 拆为三个独立 Controller，避免 God Object。

```
                                    GamepadInputInterpreter
                                            │
                                   emit inputEvent(InputEvent)
                                            │
                    ┌───────────────────────┼───────────────────────┐
                    │                       │                       │
                    ▼                       ▼                       ▼
        ┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐
        │ AxisSelection    │   │ MotionController │   │ ButtonController │
        │ Controller       │   │                  │   │                  │
        │                 │   │ ★持有模式状态     │   │                  │
        │ 消费 AxisSelect  │   │ 消费 Motion      │   │ 消费 Button      │
        │ → AxisSelModel   │   │ → InputRouter    │   │ → InputRouter    │
        └─────────────────┘   └─────────────────┘   └─────────────────┘
                    │                       │                       │
                    └───────────────────────┼───────────────────────┘
                                            │
                                            ▼
                                    ┌─────────────────┐
                                    │   InputRouter    │  (application 层)
                                    │                  │
                                    │ 仅路由，无状态   │
                                    │ jogForward()     │
                                    │ jogStopForward() │
                                    │ relMove(step)    │
                                    │ enable()         │
                                    │ stop()           │
                                    │ zeroAbsolute()   │
                                    └────────┬────────┘
                                             │
                                             ▼
                                    AxisViewModelCore
```

#### 4.3.1 AxisSelectionController

**职责**：消费 `InputEvent::AxisSelect` 类型事件，驱动 `AxisSelectionModel`。

```cpp
// presentation/input/AxisSelectionController.h
#pragma once

#include <QObject>
#include "InputEvent.h"

class AxisSelectionModel;

/// @brief 消费 AxisSelect InputEvent，驱动 AxisSelectionModel
/// 职责极窄：只处理 event.type == AxisSelect
class AxisSelectionController : public QObject
{
    Q_OBJECT

public:
    explicit AxisSelectionController(AxisSelectionModel* model, QObject* parent = nullptr);

public slots:
    /// @brief 接收来自 GamepadInputInterpreter 的统一 InputEvent
    void onInputEvent(const InputEvent& event);

private:
    AxisSelectionModel* m_model;
};
```

```cpp
// presentation/input/AxisSelectionController.cpp
void AxisSelectionController::onInputEvent(const InputEvent& event) {
    if (event.type != InputEvent::Type::AxisSelect) return;

    if (event.axisDir == AxisSelectDirection::Left) {
        m_model->selectLeft();
    } else {
        m_model->selectRight();
    }
}
```

#### 4.3.2 MotionController

**职责**：消费 `InputEvent::Motion` 类型事件，根据当前模式路由到 `InputRouter`。**这是唯一持有控制模式（JOG/Position）状态的地方。**

```cpp
// presentation/input/MotionController.h
#pragma once

#include <QObject>
#include "InputEvent.h"

class InputRouter;

/// @brief 消费 Motion InputEvent
/// 唯一持有控制模式 (JOG / Position) 状态的组件
class MotionController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString controlMode READ controlModeName NOTIFY controlModeChanged)

public:
    enum class ControlMode { JOG, Position };

    explicit MotionController(InputRouter* router, QObject* parent = nullptr);

    ControlMode currentMode() const { return m_controlMode; }
    QString controlModeName() const;

    /// @brief 切换控制模式（由 ButtonController 委托调用，或直接供 QML 调用）
    void toggleMode();

public slots:
    /// @brief 接收来自 GamepadInputInterpreter 的统一 InputEvent
    void onInputEvent(const InputEvent& event);

signals:
    void controlModeChanged();

private:
    InputRouter* m_router;
    ControlMode m_controlMode = ControlMode::JOG;

    // JOG 模式下的方向跟踪（防重复下发同一方向）
    bool m_jogForwardActive = false;
    bool m_jogBackwardActive = false;

    void handleJogMotion(const InputEvent& event);
    void handlePositionMotion(const InputEvent& event);
};
```

```cpp
// presentation/input/MotionController.cpp
void MotionController::onInputEvent(const InputEvent& event) {
    if (event.type != InputEvent::Type::Motion) return;

    if (m_controlMode == ControlMode::JOG) {
        handleJogMotion(event);
    } else {
        handlePositionMotion(event);
    }
}

void MotionController::handleJogMotion(const InputEvent& event) {
    if (event.motionType == MotionEventType::Pressed) {
        if (event.motionDir == MotionDirection::Forward && !m_jogForwardActive) {
            m_router->jogForward();
            m_jogForwardActive = true;
        } else if (event.motionDir == MotionDirection::Backward && !m_jogBackwardActive) {
            m_router->jogBackward();
            m_jogBackwardActive = true;
        }
    } else {  // Released
        if (event.motionDir == MotionDirection::Forward && m_jogForwardActive) {
            m_router->jogStopForward();
            m_jogForwardActive = false;
        } else if (event.motionDir == MotionDirection::Backward && m_jogBackwardActive) {
            m_router->jogStopBackward();
            m_jogBackwardActive = false;
        }
    }
}

void MotionController::handlePositionMotion(const InputEvent& event) {
    if (event.motionType == MotionEventType::Pressed) {
        const double step = 1.0;  // TODO: 可配置步长
        double distance = (event.motionDir == MotionDirection::Forward) ? +step : -step;
        m_router->relMove(distance);
    }
    // Position 模式下 Released 无操作
}

void MotionController::toggleMode() {
    m_controlMode = (m_controlMode == ControlMode::JOG) ? ControlMode::Position : ControlMode::JOG;
    // 切换模式时清零 JOG 方向状态，避免跨模式残留
    m_jogForwardActive = false;
    m_jogBackwardActive = false;
    emit controlModeChanged();
}
```

#### 4.3.3 ButtonController

**职责**：消费 `InputEvent::Button` 类型事件，路由到 `InputRouter`。Y 按钮委托给 `MotionController::toggleMode()`。

```cpp
// presentation/input/ButtonController.h
#pragma once

#include <QObject>
#include "InputEvent.h"

class InputRouter;
class MotionController;

/// @brief 消费 Button InputEvent，路由到对应的业务操作
/// 职责极窄：只处理 event.type == Button
class ButtonController : public QObject
{
    Q_OBJECT

public:
    explicit ButtonController(InputRouter* router,
                              MotionController* motionCtrl,
                              QObject* parent = nullptr);

public slots:
    void onInputEvent(const InputEvent& event);

private:
    InputRouter* m_router;
    MotionController* m_motionController;  // 仅用于 Y 按钮模式切换
};
```

```cpp
// presentation/input/ButtonController.cpp
void ButtonController::onInputEvent(const InputEvent& event) {
    if (event.type != InputEvent::Type::Button) return;
    if (!event.buttonPressed) return;  // 仅响应按下，忽略释放

    switch (event.button) {
    case GamepadButton::A:
        m_router->enable();
        break;
    case GamepadButton::B:
        m_router->stop();
        break;
    case GamepadButton::X:
        m_router->zeroAbsolute();
        break;
    case GamepadButton::Y:
        m_motionController->toggleMode();
        break;
    }
}
```

---

### 4.4 应用层：InputRouter 路由

**职责**：**仅路由，无业务状态**。将语义方法调用透传至当前 ViewModel。

这是整个输入链路中唯一知道"ViewModel 有哪些方法"的地方，但它不持有任何模式状态、方向状态或业务判断逻辑。

```cpp
// application/InputRouter.h
#pragma once

#include <QObject>
#include "domain/entity/Axis.h"  // Direction

class AxisViewModelCore;

/// @brief 纯路由层：将语义化操作透传至当前 ViewModel
/// 禁止持有任何业务状态（模式、方向跟踪等）
class InputRouter : public QObject
{
    Q_OBJECT

public:
    explicit InputRouter(QObject* parent = nullptr);

    /// @brief 当 QML currentAxis 变更时调用，切换路由目标 ViewModel
    void setCurrentViewModel(AxisViewModelCore* vm);

    // ── JOG 操作 ──
    void jogForward();
    void jogBackward();
    void jogStopForward();
    void jogStopBackward();

    // ── Position 操作 ──
    void relMove(double distance);

    // ── 通用操作 ──
    void enable();
    void stop();
    void zeroAbsolute();

private:
    AxisViewModelCore* m_currentViewModel = nullptr;
};
```

```cpp
// application/InputRouter.cpp
void InputRouter::setCurrentViewModel(AxisViewModelCore* vm) {
    m_currentViewModel = vm;
}

void InputRouter::jogForward() {
    if (m_currentViewModel) m_currentViewModel->jog(Direction::Forward);
}

void InputRouter::jogBackward() {
    if (m_currentViewModel) m_currentViewModel->jog(Direction::Backward);
}

void InputRouter::jogStopForward() {
    if (m_currentViewModel) m_currentViewModel->jogStop(Direction::Forward);
}

void InputRouter::jogStopBackward() {
    if (m_currentViewModel) m_currentViewModel->jogStop(Direction::Backward);
}

void InputRouter::relMove(double distance) {
    if (m_currentViewModel) {
        if (m_currentViewModel->setRelTarget(distance)) {
            m_currentViewModel->triggerRelMove();
        }
    }
}

void InputRouter::enable() {
    if (m_currentViewModel) m_currentViewModel->enable(true);
}

void InputRouter::stop() {
    if (m_currentViewModel) m_currentViewModel->stop();
}

void InputRouter::zeroAbsolute() {
    if (m_currentViewModel) m_currentViewModel->zeroAbsolutePosition();
}
```

---

### 4.5 业务层：无感知复用

**业务层完全不需要修改**。以下是现有的接口，摇杆操作将直接复用：

| 摇杆操作 | 调用的现有 ViewModel 方法 | 内部链路 |
|---------|--------------------------|---------|
| 右摇杆 ↑ (JOG) | `vm.jog(Direction::Forward)` | → JogOrchestrator → Axis → Driver |
| 右摇杆 ↓ (JOG) | `vm.jog(Direction::Backward)` | → JogOrchestrator → Axis → Driver |
| 右摇杆回中 | `vm.jogStop(direction)` | → JogOrchestrator.stopJog() |
| 右摇杆 ↑ (Position) | `vm.setRelTarget(+step)` + `vm.triggerRelMove()` | → AutoRelMoveOrchestrator |
| 按钮 A | `vm.enable(true)` | → EnableUseCase |
| 按钮 B | `vm.stop()` | → StopAxisUseCase |
| 按钮 X | `vm.zeroAbsolutePosition()` | → Axis → Driver |
| 按钮 Y | (MotionController 内部切换模式) | 无业务调用 |

---

## 5. 关键模型设计

### 5.1 死区与回中检测

```
         -1.0         0.0          +1.0
          │            │            │
  ←───────┼────────────┼────────────┼───────→  ry (或 lx)
          │   Deadzone │            │
          │  [-0.3,    │            │
          │   +0.3]    │            │
          │            │            │
    Backward        Neutral      Forward
```

**状态机**：

```
                    ry < -0.3
    Neutral ────────────────────→ BackwardActive
                                      │
                      |ry| < 0.3      │
    Neutral ←─────────────────────────┘

                    ry > +0.3
    Neutral ────────────────────→ ForwardActive
                                      │
                      |ry| < 0.3      │
    Neutral ←─────────────────────────┘
```

### 5.2 左摇杆去抖

左摇杆选轴需要去抖，避免一次拨动触发多次切换：

```
时间线：
    拨动 → emit → 启动 300ms 冷却 → 冷却期内忽略 → 冷却结束 → 可再次触发
```

### 5.3 AxisSelectionModel 轴列表

```cpp
// 初始只暴露 Y/Z/R 给摇杆
const std::vector<AxisId> kGamepadSelectableAxes = {
    AxisId::Y,
    AxisId::Z,
    AxisId::R
};
```

未来可通过长按组合键扩展到 X/X1/X2。

---

## 6. 模式切换设计

### 6.1 两种控制模式

| 模式 | 右摇杆行为 | 对应业务操作 |
|------|-----------|-------------|
| **JOG** | 推→动，回→停 | `jog()` / `jogStop()` |
| **Position** | 推→相对移动一步 | `setRelTarget(±step)` + `triggerRelMove()` |

### 6.2 模式切换触发

- 按钮 Y 按下 → `ButtonController` 委托 `MotionController::toggleMode()`
- QML 显示当前模式（绑定 `MotionController::controlModeName`）
- 模式状态 **仅存在于 `MotionController`**，不污染 `InputRouter` 或业务层

### 6.3 屏幕 UI 反馈

```text
┌─────────────────────────────┐
│ 当前轴：Z                   │
│ 位置：125.321 mm            │
│ 状态：Jogging               │
│                             │
│ 模式：JOG                   │  ← 按钮 Y 切换为 Position
│                             │
│ 点动速度：50 mm/s           │
│                             │
│ [Y] 模式切换                │
└─────────────────────────────┘
```

---

## 7. 文件结构规划

```
servoV6/
│
├── presentation/                           # 表现层
│   ├── CMakeLists.txt
│   ├── viewmodel/
│   │   ├── QtAxisViewModel.h/.cpp          # ⚠️ 少量扩展：暴露 currentMode 等属性
│   │   ├── AxisViewModelCore.h/.cpp        # 不变
│   │   └── ...
│   ├── qml/
│   │   ├── ...
│   │   └── views/
│   │       └── MainDashboard.qml           # ⚠️ 少量扩展：集成 InputRouter + Controllers
│   │
│   └── input/                              # ★ 新增：输入抽象子包
│       ├── InputEvent.h                    # 统一输入事件类型定义（零依赖）
│       ├── GamepadInputInterpreter.h       # 原始数据 → InputEvent 翻译器
│       ├── GamepadInputInterpreter.cpp
│       ├── AxisSelectionModel.h            # 轴选择数据模型
│       ├── AxisSelectionModel.cpp
│       ├── AxisSelectionController.h       # 左摇杆 → 选轴操作
│       ├── AxisSelectionController.cpp
│       ├── MotionController.h              # 右摇杆 → 运动操作（含模式状态）
│       ├── MotionController.cpp
│       ├── ButtonController.h              # 按钮 → 使能/停止/回零/模式
│       └── ButtonController.cpp
│
├── application/                            # 应用层
│   ├── CMakeLists.txt
│   ├── InputRouter.h                       # ★ 新增：纯路由层（仅透传，无状态）
│   ├── InputRouter.cpp
│   ├── axis/                               # 不变
│   │   ├── JogAxisUseCase.h
│   │   ├── EnableUseCase.h
│   │   ├── StopAxisUseCase.h
│   │   └── ...
│   ├── policy/                             # 不变
│   │   ├── JogOrchestrator.h
│   │   ├── AutoRelMoveOrchestrator.h
│   │   └── ...
│   └── ...
│
├── domain/                                 # 领域层（完全不变）
│   ├── entity/Axis.h/.cpp
│   ├── entity/AxisId.h
│   └── ...
│
├── infrastructure/                         # 基础设施层
│   ├── CMakeLists.txt
│   ├── FakeAxisDriver.h                    # 不变
│   ├── FakePLC.h                           # 不变
│   ├── joystick/                           # ★ 新增：摇杆 HAL 子包
│   │   ├── AndroidGamepadJoystick.h        # JNI 桥接 C++ 端（HAL 实现）
│   │   └── AndroidGamepadJoystick.cpp
│   └── ...
│
└── android/src/org/qtproject/gamepad/      # 平台层（Android JNI）
    ├── MainActivity.java                   # Qt Activity
    └── GamepadBridge.java                  # JNI 桥接类
```

### 7.1 关键新增文件说明

| 文件 | 层 | 职责 | 依赖 |
|------|-----|------|------|
| `InputEvent.h` | presentation/input | 统一输入事件类型定义 | 零外部依赖 |
| `GamepadInputInterpreter` | presentation/input | 死区/去抖/边缘触发 → emit `InputEvent` | `AndroidGamepadJoystick` |
| `AxisSelectionModel` | presentation/input | 轴列表 + 当前索引管理 | `AxisId` |
| `AxisSelectionController` | presentation/input | 消费 `AxisSelect` InputEvent | `AxisSelectionModel` |
| `MotionController` | presentation/input | 消费 `Motion` InputEvent，持有模式状态 | `InputRouter` |
| `ButtonController` | presentation/input | 消费 `Button` InputEvent | `InputRouter` + `MotionController` |
| `InputRouter` | application | 纯路由：语义方法 → 当前 ViewModel | `AxisViewModelCore` |
| `AndroidGamepadJoystick` | infrastructure/joystick | JNI 数据捕获 HAL | Qt + JNI |
| `GamepadBridge.java` | 平台层 | Android MotionEvent/KeyEvent 监听 | Android SDK |
| `MainActivity.java` | 平台层 | 安装 GamepadBridge | QtActivity + GamepadBridge |

---

## 8. 实现路线图

### 阶段 0：JNI 链路验证（已完成 ✅）

- [x] `MainActivity.java` — Qt Activity，设置焦点，安装 GamepadBridge
- [x] `GamepadBridge.java` — MotionEvent + KeyEvent 监听，JNI native 方法声明
- [x] `AndroidGamepad` → `AndroidGamepadJoystick` 重命名并移入 `infrastructure/joystick/` — C++ 单例，Q_PROPERTY 暴露，JNI 实现
- [x] 日志验证：`Axis LX=... LY=... RX=... RY=... LT=... RT=...`

### 阶段 1：输入抽象层核心

- [ ] **创建 `presentation/input/` 目录**，添加至 `presentation/CMakeLists.txt`
- [ ] **实现 `InputEvent.h`**：零依赖头文件，定义枚举和 `InputEvent` struct
- [ ] **实现 `GamepadInputInterpreter`**：
  - 连接 `AndroidGamepadJoystick::changed()` 信号
  - 左摇杆死区 + 去抖逻辑
  - 右摇杆死区 + 边缘检测状态机
  - 按钮边缘检测
  - emit `inputEvent(InputEvent)`
- [ ] **实现 `AxisSelectionModel`**：
  - 维护 `std::vector<AxisId>` 和 `m_currentIndex`
  - `selectLeft()` / `selectRight()` 循环切换
  - emit `currentAxisChanged(AxisId)`

### 阶段 2：Controller 三件套 + InputRouter

- [ ] **实现 `InputRouter`**（`application/InputRouter.h/.cpp`）：
  - 持有 `AxisViewModelCore*` 引用
  - 暴露 `jogForward()` / `jogBackward()` / `jogStopForward()` / `jogStopBackward()`
  - 暴露 `relMove(distance)` / `enable()` / `stop()` / `zeroAbsolute()`
  - 纯透传，无任何业务状态
- [ ] **实现 `AxisSelectionController`**：
  - 消费 `InputEvent::AxisSelect` → 驱动 `AxisSelectionModel`
- [ ] **实现 `MotionController`**：
  - 消费 `InputEvent::Motion` → 调用 `InputRouter`
  - 持有 `ControlMode` 状态（JOG / Position）
  - `m_jogForwardActive` / `m_jogBackwardActive` 防重复
  - `toggleMode()` 供 `ButtonController` 委托
- [ ] **实现 `ButtonController`**：
  - 消费 `InputEvent::Button` → 调用 `InputRouter`（A/B/X）+ `MotionController::toggleMode()`（Y）

### 阶段 3：表现层集成

- [ ] **修改 `Main.qml`**：
  - 创建 `InputRouter` + Controllers 实例并连接信号
  - `currentAxis` 变更时调用 `InputRouter::setCurrentViewModel()`
  - 绑定 `MotionController::controlModeName` 到 UI
- [ ] **修改 `QtAxisViewModel`**（如需）：
  - 暴露可能需要的额外 Q_PROPERTY（如 `isLoading` 绑定到 Position 操作）
- [ ] **端到端联调验证**：
  - 左摇杆选轴 → QML 轴切换 + InputRouter ViewModel 切换
  - 右摇杆运动 → JOG/Position 两种模式验证
  - 按钮操作 → 使能/停止/回零/模式切换

### 阶段 4：测试与打磨

- [ ] **单元测试 `GamepadInputInterpreter`**：
  - 死区精度测试（±0.29 不应触发，±0.31 应触发）
  - 边缘检测状态机测试（Neutral→Forward→Neutral→Backward...）
  - 去抖逻辑测试（300ms 内多次拨动只触发一次）
- [ ] **单元测试 `AxisSelectionModel`**：
  - 循环切换正确性（Y→Z→R→Y）
  - `setCurrentAxis()` 直接切换
- [ ] **单元测试 `MotionController`**：
  - JOG 模式防重复下发
  - 模式切换后状态清零
  - Position 模式 Released 无操作
- [ ] **集成测试**：
  - 使用 `FakePLC` + `FakeAxisDriver` 验证完整链路
  - `InputEvent → GamepadInputInterpreter → Controller → InputRouter → ViewModel → Orchestrator → Driver → FakePLC`
- [ ] **参数调优**：
  - 死区阈值（初始 0.3）
  - 去抖间隔（初始 300ms）
  - Position 模式步长（初始 1.0mm）

### 阶段 5（未来）：多输入设备统一

- [ ] **键盘映射**：W/A/S/D → `InputEvent::Motion`（新的 `KeyboardInputInterpreter` 实现）
- [ ] **触摸屏映射**：QML Jog+/Jog- 按钮 → `InputEvent::Motion`
- [ ] **USB 手柄**：新的 HAL 实现（如 `LinuxGamepadJoystick`），复用同一套 `GamepadInputInterpreter`

**多设备接入对比**：

```
现有架构：
    AndroidGamepadJoystick ──→ GamepadInputInterpreter ──→ InputEvent ──→ Controllers ──→ InputRouter ──→ 业务 (不变)

未来接入键盘：
    KeyboardListener ──→ KeyboardInputInterpreter ──→ InputEvent ──→ Controllers ──→ InputRouter ──→ 业务 (不变)

未来接入触摸屏：
    QmlTouchButtons ──→ TouchInputInterpreter ──→ InputEvent ──→ Controllers ──→ InputRouter ──→ 业务 (不变)
```

---

## 附录 A：与现有代码的关系总结

| 现有组件 | 是否修改 | 说明 |
|---------|---------|------|
| `domain/entity/Axis.h/.cpp` | ❌ 不变 | 领域层完全不感知输入来源 |
| `domain/entity/AxisId.h` | ❌ 不变 | — |
| `application/axis/JogAxisUseCase.h` | ❌ 不变 | — |
| `application/axis/EnableUseCase.h` | ❌ 不变 | — |
| `application/axis/StopAxisUseCase.h` | ❌ 不变 | — |
| `application/axis/MoveRelativeUseCase.h` | ❌ 不变 | Position 模式复用 |
| `application/policy/JogOrchestrator.h` | ❌ 不变 | — |
| `application/policy/AutoRelMoveOrchestrator.h` | ❌ 不变 | Position 模式复用 |
| `presentation/viewmodel/AxisViewModelCore.h/.cpp` | ❌ 不变 | 摇杆通过现有接口操作 |
| `presentation/viewmodel/QtAxisViewModel.h/.cpp` | ⚠️ 少量扩展 | 可能需暴露模式/选轴相关属性 |
| `presentation/qml/views/MainDashboard.qml` | ⚠️ 少量扩展 | 集成 Controllers + InputRouter |
| `infrastructure/` | ⚠️ 新增子目录 | 新增 `infrastructure/joystick/` |

---

## 附录 B：职责边界设计原则总结

```
God Object 反模式 (原 InputToBusinessMapper)：

    ┌─────────────────────────────────────┐
    │      InputToBusinessMapper          │
    │  - 接收所有 InputEvent              │
    │  - 管理模式状态                     │
    │  - 管理 JOG 方向状态                │
    │  - 管理轴选择                       │
    │  - 调用 ViewModel                   │
    │  - 未来还要加：长按/组合键/龙门轴   │
    │  → 越来越大，越来越难测试           │
    └─────────────────────────────────────┘

正确做法 (Controller 三件套 + InputRouter)：

    ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
    │  AxisSelCtrl │  │ MotionCtrl   │  │ ButtonCtrl   │
    │  只做选轴    │  │ 只做运动     │  │ 只做按钮     │
    │  极窄职责   │  │ (含模式状态) │  │  极窄职责   │
    └──────┬───────┘  └──────┬───────┘  └──────┬───────┘
           │                 │                 │
           └─────────────────┼─────────────────┘
                             │
                    ┌────────┴────────┐
                    │   InputRouter   │
                    │   仅路由透传    │
                    │   无任何状态    │
                    └────────┬────────┘
                             │
                     AxisViewModelCore
```

**核心规则**：

1. Interpreter 只管"翻译"（死区/去抖/边缘触发），输出 `InputEvent`
2. Controller 只管"消费分配"：每种 InputEvent Type 只有一个 Controller 负责
3. InputRouter 只管"透传"：语义方法 → ViewModel，禁止持有任何状态
4. 状态（模式、方向跟踪）只存在于需要的 Controller 内部，不扩散

---

## 附录 C：修订说明

本版本相对于初版的三个核心调整：

| 调整 | 初版 | 修订版 | 原因 |
|------|------|--------|------|
| 输入层位置 | 独立 `input/` 目录 | `presentation/input/` 子包 | Input 本质是用户输入，属于 Presentation 层 |
| God Object | `InputToBusinessMapper` 统管一切 | 拆为 `AxisSelectionController` + `MotionController` + `ButtonController` + `InputRouter` | 单一职责，避免未来膨胀 |
| HAL 位置 | `AndroidGamepad` 放在 `input/` | `AndroidGamepadJoystick` 放在 `infrastructure/joystick/` | JNI 实现属于基础设施层，与输入抽象分离 |

---

*文档版本：v1.1 (修订版)*  
*最后更新：2026-06-08*  
*适用项目：servoV6*