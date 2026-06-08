#pragma once

#include <QObject>
#include <unordered_map>
#include "InputEvent.h"
#include "domain/entity/AxisId.h"

class QtAxisViewModel;
class AxisSelectionModel;
class GamepadInputInterpreter;

/// @brief 消费 Motion InputEvent，将右摇杆映射到当前轴的 StartJog/StopJog 或定位移动
///
/// JOG 模式：
///   右摇杆向上（Forward Pressed）  → currentAxis.jogPositivePressed()   = StartJog(axis)
///   右摇杆回中（Forward Released） → currentAxis.jogPositiveReleased()  = StopJog(axis)
///   右摇杆向下（Backward Pressed）  → currentAxis.jogNegativePressed()   = StartJog(axis, Backward)
///   右摇杆回中（Backward Released） → currentAxis.jogNegativeReleased()  = StopJog(axis)
///
/// Position 模式：
///   右摇杆按下（Pressed） → 无操作（仅记录方向）
///   右摇杆松开（Released）：
///     绝对子模式 → triggerAbsMove()
///     相对子模式 → setRelTarget(±step) + triggerRelMove()
///
/// 跨轴跳跃保护：
///   当用户在摇杆推动期间切换轴时，先对旧轴发送 Released，再对新轴重放当前摇杆方向
class MotionController : public QObject
{
    Q_OBJECT

    /// @brief 控制模式：0=JOG（点动），1=Position（定位）
    Q_PROPERTY(int controlMode READ controlMode WRITE setControlMode NOTIFY controlModeChanged)

    /// @brief 定位子模式：true=绝对定位，false=相对定位（仅 Position 模式下有意义）
    Q_PROPERTY(bool isAbsolute READ isAbsolute WRITE setIsAbsolute NOTIFY isAbsoluteChanged)

public:
    /// @param interpreter  摇杆事件源（emit inputEvent）
    /// @param axisModel    当前选轴模型（emit currentAxisChanged）
    explicit MotionController(GamepadInputInterpreter* interpreter,
                              AxisSelectionModel* axisModel,
                              QObject* parent = nullptr);

    /// @brief 注册轴 → ViewModel 映射（供 onInputEvent 查找对应 ViewModel 发送指令）
    void registerAxis(AxisId id, QtAxisViewModel* vm);

    int controlMode() const { return m_controlMode; }
    Q_INVOKABLE void setControlMode(int mode);

    bool isAbsolute() const { return m_isAbsolute; }
    Q_INVOKABLE void setIsAbsolute(bool absolute);

    /// @brief 切换控制模式（JOG ↔ Position），由 Y 按钮或 QML 调用
    Q_INVOKABLE void toggleMode();

public slots:
    /// @brief 消费 Motion 类型的 InputEvent
    void onInputEvent(const InputEvent& event);

    /// @brief 轴切换时：先释放旧轴的活跃 jog，再对新轴重放当前摇杆方向
    void onCurrentAxisChanged(AxisId newAxis);

signals:
    void controlModeChanged();
    void isAbsoluteChanged();

private:
    /// @brief 释放当前轴的活跃 jog（给旧轴发 Released）
    void releaseCurrentMotion();

    /// @brief 对当前轴发起指定方向的 jog（jogPositivePressed 或 jogNegativePressed）
    void pressMotion(MotionDirection dir);

    /// @brief JOG 模式下的 Motion 事件处理
    void handleJogMotion(const InputEvent& event);

    /// @brief Position 模式下的 Motion 事件处理（仅 Released 时触发移动）
    void handlePositionMotion(const InputEvent& event);

    AxisSelectionModel* m_axisModel;
    std::unordered_map<AxisId, QtAxisViewModel*> m_vmMap;

    AxisId m_currentAxis = AxisId::Y;

    // 记录当前活跃方向（上一次 Non-Neutral 的方向），用于轴切换时"重放"
    MotionDirection m_activeMotionDir = MotionDirection::Forward;  // 任意初始值，会在首次事件时覆盖
    bool m_motionActive = false;  // true = 摇杆当前在死区外

    // ── 控制模式状态 ──
    int m_controlMode = 0;     // 0=JOG, 1=Position
    bool m_isAbsolute = true;  // true=绝对定位, false=相对定位（仅 Position 模式）
};