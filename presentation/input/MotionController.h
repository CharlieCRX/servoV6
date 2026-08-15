#pragma once

#include <QObject>
#include <QElapsedTimer>
#include "InputEvent.h"
#include "domain/entity/AxisId.h"

#include "application_vnext/control/ControlCommand.h"

class AxisSelectionModel;
class GamepadInputInterpreter;

namespace application_vnext::control {
class MotionControlService;
}  // namespace application_vnext::control

/// @brief 消费 Motion InputEvent，把右摇杆映射为 ControlCommand 提交给统一协调层
///
/// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 4：
/// `MotionController` 不再持有 QtAxisViewModel、不再直接写 PLC，只负责选轴 / 死区 /
/// 模式等摇杆侧状态，把业务意图命令（source=Joystick）经 MotionControlService
/// 的唯一命令入口 submit()，由协调层仲裁后统一执行。
///
/// JOG 模式：
///   右摇杆向上（Forward Pressed）  → StartJogForward (Joystick 源)
///   右摇杆回中（Forward Released） → StopJog (Joystick 源)
///   右摇杆向下（Backward Pressed） → StartJogBackward
///   右摇杆回中（Backward Released）→ StopJog
///
/// Position 模式：
///   右摇杆按下（Pressed） → 无操作（仅记录方向）
///   右摇杆松开（Released）：
///     绝对子模式 → StartAbsMove(target=快照 absMoveTarget)
///     相对子模式 → StartRelMove(target=相对步进)
///
/// 跨轴跳跃保护：用户在摇杆推动期间切换轴时，先对旧轴提交 StopJog，再对新轴重放
/// 当前摇杆方向（StartJogForward/Backward）。
class MotionController : public QObject
{
    Q_OBJECT

    /// @brief 控制模式：0=JOG（点动），1=Position（定位）
    Q_PROPERTY(int controlMode READ controlMode WRITE setControlMode NOTIFY controlModeChanged)

    /// @brief 定位子模式：true=绝对定位，false=相对定位（仅 Position 模式下有意义）
    Q_PROPERTY(bool isAbsolute READ isAbsolute WRITE setIsAbsolute NOTIFY isAbsoluteChanged)

    /// @brief JOG 点动活跃方向：0=无点动, 1=前进活跃, -1=后退活跃
    /// 同时由摇杆 (handleJogMotion) 和 QML 按钮 (onPressed/onReleased) 写入，
    /// QML 按钮的 isActive 绑定此属性以展示正确的选中视觉反馈。
    Q_PROPERTY(int jogActiveDirection READ jogActiveDirection WRITE setJogActiveDirection NOTIFY jogActiveDirectionChanged)

public:
    /// @param interpreter  摇杆事件源（emit inputEvent）
    /// @param axisModel    当前选轴模型（emit currentAxisChanged）
    /// @param service      唯一控制协调层入口（可传 nullptr 以便 UI 预览/离线安全降级；
    ///                     注入后摇杆才真正提交命令）
    explicit MotionController(GamepadInputInterpreter* interpreter,
                              AxisSelectionModel* axisModel,
                              application_vnext::control::MotionControlService* service,
                              QObject* parent = nullptr);

    int controlMode() const { return m_controlMode; }
    Q_INVOKABLE void setControlMode(int mode);

    bool isAbsolute() const { return m_isAbsolute; }
    Q_INVOKABLE void setIsAbsolute(bool absolute);

    int jogActiveDirection() const { return m_jogActiveDirection; }
    Q_INVOKABLE void setJogActiveDirection(int dir);

    /// @brief 切换控制模式（JOG ↔ Position），由 Y 按钮或 QML 调用
    Q_INVOKABLE void toggleMode();

public slots:
    /// @brief 消费 Motion 类型的 InputEvent
    void onInputEvent(const InputEvent& event);

    /// @brief 轴切换时：先向旧轴提交 StopJog，再对新轴重放当前摇杆方向
    void onCurrentAxisChanged(AxisId newAxis);

signals:
    void controlModeChanged();
    void isAbsoluteChanged();
    void jogActiveDirectionChanged();

private:
    /// @brief 释放当前轴的活跃 jog（给旧轴提交 StopJog）
    void releaseCurrentMotion();

    /// @brief 对当前轴发起指定方向的 jog（StartJogForward 或 StartJogBackward）
    void pressMotion(MotionDirection dir);

    /// @brief JOG 模式下的 Motion 事件处理
    void handleJogMotion(const InputEvent& event);

    /// @brief Position 模式下的 Motion 事件处理（仅 Released 时触发移动）
    void handlePositionMotion(const InputEvent& event);

    /// @brief 把 AxisId 映射为统一业务目标（A 组 + 功能角色），用于构造命令
    application_vnext::control::AxisTarget currentAxisTarget() const;

    /// @brief 提交一条 Joystick 源命令到协调层（service 为空时仅日志、不写 PLC）
    void submit(application_vnext::control::ControlCommand cmd);

    /// @brief 相对定位步进距离（EU），可由 QML 预填；默认 1.0
    double m_relStep = 1.0;

    AxisSelectionModel* m_axisModel;
    application_vnext::control::MotionControlService* m_service;

    AxisId m_currentAxis = AxisId::Y;

    // 记录当前活跃方向（上一次 Non-Neutral 的方向），用于轴切换时"重放"
    MotionDirection m_activeMotionDir = MotionDirection::Forward;  // 任意初始值，会在首次事件时覆盖
    bool m_motionActive = false;  // true = 摇杆当前在死区外

    // ── 控制模式状态 ──
    int m_controlMode = 0;     // 0=JOG, 1=Position
    bool m_isAbsolute = true;  // true=绝对定位, false=相对定位（仅 Position 模式）

    // ── JOG 活跃方向（QML 按钮视觉反馈）──
    int m_jogActiveDirection = 0;  // 0=无点动, 1=前进活跃, -1=后退活跃

    // ── 快速摆动检测（防误触） ──
    // 当方向 Released 后极短时间内又收到反向 Pressed → 视为取消操作，拒绝启动新方向
    QElapsedTimer m_jogReleaseTimer;
    static constexpr qint64 kRapidWiggleThresholdMs = 150;  // 150ms 内反向摆动 = 取消
};
