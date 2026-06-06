# 摇杆控制逻辑分析

## 整体架构

摇杆控制涉及三个核心类，形成一条完整的信号链路：

```
[物理游戏手柄] → [InputManager] → [JoystickControllerManager] → [MotorCtrl]
```

---

## 1. InputManager（输入管理器）— `inputmanager.h` / `inputmanager.cpp`

### 职责
负责与 Qt `QGamepad` 模块交互，将物理摇杆的模拟轴值转换为离散方向信号。

### 核心枚举 `Direction`
```cpp
enum class Direction {
    BACKWARD = -1,
    BACKWARD_NEUTRAL = 0,
    FORWARD = 1,
    FORWARD_NEUTRAL,
    DEADZONE
};
```
定义了5种状态：前进、后退、前进回中（释放）、后退回中（释放）、死区。

### 初始化流程
1. 构造函数连接 `QGamepadManager` 的 `gamepadConnected`/`gamepadDisconnected` 信号。
2. 调用 `initializeGamepad()` → 如果已连接手柄，创建 `QGamepad` 对象，调用 `setupJoystickHandlers()`。
3. `handleGamepadConnected()` 在运行时动态检测新连接的手柄。

### 摇杆轴绑定 `setupJoystickHandlers()`
只监听4个轴（左右摇杆的 X/Y）：
```
左摇杆 X → axisLeftXChanged  (水平)
左摇杆 Y → axisLeftYChanged  (垂直)
右摇杆 X → axisRightXChanged (水平)
右摇杆 Y → axisRightYChanged (垂直)
```

### 方向计算 `getJoystickDirection()`
核心逻辑：
1. **死区判断**：`|value| < deadZone(默认1)` → 进入归中逻辑。根据上一次有效方向，返回 `FORWARD_NEUTRAL` 或 `BACKWARD_NEUTRAL`，若从未有效则返回 `DEADZONE`。
2. **方向判定**：
   - 水平轴 (X): `value > 0` → FORWARD, `value < 0` → BACKWARD
   - 垂直轴 (Y): `value < 0` → FORWARD（上推为负值）, `value > 0` → BACKWARD
3. 记录当前有效方向到 `m_lastValidDirections` map 中。

### 跨轴跳跃保护（关键修复）
```cpp
if (lastDir == Direction::FORWARD && dir == Direction::BACKWARD) {
    emit joyForwardMoveEnd();   // 先松开"前进"
} else if (lastDir == Direction::BACKWARD && dir == Direction::FORWARD) {
    emit joyBackwardMoveEnd();  // 先松开"后退"
}
```
当摇杆从前进直接跳到后退（不经过死区）时，**先补发上一个方向的松开信号**，确保电机收到停止指令。

### 发出的信号
| 信号                            | 触发条件                | 含义       |
| ------------------------------- | ----------------------- | ---------- |
| `joyForwardMoveStart()`         | 进入FORWARD             | 开始前进   |
| `joyForwardMoveEnd()`           | FORWARD_NEUTRAL 或跨轴  | 停止前进   |
| `joyBackwardMoveStart()`        | 进入BACKWARD            | 开始后退   |
| `joyBackwardMoveEnd()`          | BACKWARD_NEUTRAL 或跨轴 | 停止后退   |
| `joystickEvent(axisKey, dir)`   | 每次非死区              | 轴事件日志 |
| `gamepadConnected(deviceId)`    | 手柄连接                | 通知       |
| `gamepadDisconnected(deviceId)` | 手柄断开                | 通知       |

---

## 2. JoystickControllerManager（摇杆控制器管理器）— `joystickcontrollermanager.h` / `.cpp`

### 职责
负责连接 `InputManager` 的信号到 `MotorCtrl` 的槽函数，并提供"启用/禁用"摇杆功能的管理（通过引用计数）。

### 核心机制：引用计数式的安全开关
- `m_disableCount`：跟踪禁用请求数量。每调用 `disable()` 一次 +1，每调用 `enable()` 一次 -1。
- **只有当 `m_disableCount == 0` 时**，才真正执行信号连接（`enable()`）或断开（`disable()`）。
- 这种设计解决了**嵌套弹窗**问题：如果有多个弹窗打开，每个弹窗都会请求禁用摇杆。只有所有弹窗关闭后，摇杆才重新启用。

### 信号连接（在 `enable()` 中）
```cpp
connect(m_inputManager, &InputManager::joyForwardMoveStart,
        m_motorCtrl, &MotorCtrl::on_actionUpRightFrontBt_pressed);
connect(m_inputManager, &InputManager::joyForwardMoveEnd,
        m_motorCtrl, &MotorCtrl::on_actionUpRightFrontBt_released);
connect(m_inputManager, &InputManager::joyBackwardMoveStart,
        m_motorCtrl, &MotorCtrl::on_actionDownLeftBackBt_pressed);
connect(m_inputManager, &InputManager::joyBackwardMoveEnd,
        m_motorCtrl, &MotorCtrl::on_actionDownLeftBackBt_released);
connect(m_inputManager, &InputManager::joystickEvent,
        this, &JoystickControllerManager::onJoystickEvent);  // 日志用
```

### 与弹窗的交互
```cpp
// 构造函数中连接（仅 Android）
connect(SMessageBox::get_instance(), &SMessageBox::dialogAboutToOpen,
        this, &JoystickControllerManager::disable);
connect(SMessageBox::get_instance(), &SMessageBox::dialogClosed,
        this, &JoystickControllerManager::enable);
connect(SMessageBox::get_instance(), &SMessageBox::forceResetAll,
        this, &JoystickControllerManager::reset);
```

### `reset()` 方法
将计数清零，安全地断开旧连接并重新连接，用于从异常状态恢复。

### 条件编译
所有关键逻辑都在 `#ifdef Q_OS_ANDROID` 下，**仅 Android/Pad 平台启用摇杆功能**。

---

## 3. MotorCtrl（电机控制器）— 摇杆相关 `motorCtrl.h` / `.cpp`

### 按钮绑定
在 `UI_setupActionButtons()` 中：
- **Pad 模式** (`DbCtrl::m_systemInfo_tb.mode == 1`)：使用 `SQPushButton::touchBegin/touchEnd`（支持触摸长按点动）
- **大屏模式** (`mode == 0`)：使用 `QPushButton::pressed/released`

### 四个关键槽函数

**按下（press）类** — 调用 `handleJogButtonPressed(dir)`：
```cpp
void MotorCtrl::on_actionUpRightFrontBt_pressed() {
    if(m_runMode_chb->isChecked()) {
        handleJogButtonPressed(+1);  // dir = +1 表示前进/上/右
    }
}

void MotorCtrl::on_actionDownLeftBackBt_pressed() {
    if(m_runMode_chb->isChecked()) {
        handleJogButtonPressed(-1);  // dir = -1 表示后退/下/左
    }
}
```
> 只有在**点动控制模式**（`m_runMode_chb->isChecked()`）下才响应。Pad 模式默认开启点动。

**松开（release）类** — 设置中止标志：
```cpp
void MotorCtrl::on_actionUpRightFrontBt_released() {
    if(m_runMode_chb->isChecked()) {
        m_abortJogRequested = true;
        // 日志: "前进点动结束"
    }
}

void MotorCtrl::on_actionDownLeftBackBt_released() {
    if(m_runMode_chb->isChecked()) {
        m_abortJogRequested = true;
        // 日志: "后退点动结束"
    }
}
```

### `handleJogButtonPressed(int dir)` — 核心点动入口
```
1. 防抖检查：如果 m_jogInProgress 或 m_jogTransactionActive → 静默忽略
2. beginJogTransaction(dir)：禁用UI其他按钮（防止误触）
3. 设置 m_abortJogRequested = false
4. checkMotorConfigurationValidity()：校验电机配置
5. isRunning() / canStartJog()：检查是否可以开始点动
6. actionJog(dir)：执行点动
```

### `actionJog(int dir)` — 执行点动
```
1. 获取当前选中电机 ID
2. 设置 m_jogInProgress = true
3. applyJogConfig(motorID, dir)：配置点动参数（速度、方向等）
4. enableJogServo(motorID, dir)：伺服使能
5. 创建工作线程 checkJog：循环监控位置/限位/中止标志
```

### 停止机制
点动停止由两个因素触发：
1. **用户松开摇杆** → `released()` 设置 `m_abortJogRequested = true` → `checkJog()` 检测到该标志 → 调用 `actionJogStop()` 停止电机
2. **物理限位/异常** → `checkJog()` 检测到限位或异常 → 自动停止

---

## 4. 完整数据流

```
物理摇杆推前
    ↓
QGamepad: axisLeftYChanged(-0.8)  [Y轴上推为负]
    ↓
InputManager::getJoystickDirection(-0.8, "Left", "Y", false)
    → |-0.8| > 1 (死区) → 非死区
    → isHorizontal=false, value<0 → FORWARD
    → emit joyForwardMoveStart()  ①
    ↓
JoystickControllerManager::enable() 建立的连接
    → MotorCtrl::on_actionUpRightFrontBt_pressed()
    → handleJogButtonPressed(+1)
    → beginJogTransaction → disableAllbt(+1) → actionJog(+1)
    ↓
电机前进运行 (checkJog 线程循环监控)
    ↓
物理摇杆回中或松手
    ↓
axisLeftYChanged(0.0) → 死区触发
    → validForwardLastOperation() → FORWARD_NEUTRAL
    → emit joyForwardMoveEnd()  ②
    ↓
MotorCtrl::on_actionUpRightFrontBt_released()
    → m_abortJogRequested = true
    → checkJog() 检测到 → actionJogStop() → 电机停止
    → endJogTransaction → releaseAllBt()
```

**关键两点**：
- ① 摇杆推前 = 前进（Y轴负值 → FORWARD）
- ② 摇杆回中 = 自动停止（死区 → FORWARD_NEUTRAL → 触发 release）

## 5. 安全设计要点

1. **引用计数禁用**：嵌套弹窗时保证摇杆在所有弹窗关闭后才恢复
2. **跨轴跳跃保护**：从前进直接跳到后退时，先发送"前进松开"再发送"后退按下"
3. **防抖保护**：`m_jogInProgress` 防止快速切换方向导致失控
4. **UI事务锁**：点动期间禁用其他按钮（`beginJogTransaction`/`endJogTransaction`）
5. **仅Android启用**：所有摇杆逻辑通过 `#ifdef Q_OS_ANDROID` 条件编译限定
6. **多轴支持**：同时监听左/右摇杆的X/Y四个轴，每个轴独立维护历史方向