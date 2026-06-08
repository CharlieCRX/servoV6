#pragma once

#include <QObject>
#include <unordered_map>
#include "InputEvent.h"
#include "domain/entity/AxisId.h"

class QtAxisViewModel;
class AxisSelectionModel;
class GamepadInputInterpreter;

/// @brief 消费 Motion InputEvent，将右摇杆映射到当前轴的 StartJog/StopJog
///
/// 逻辑通路：
///   右摇杆向上（Forward Pressed）  → currentAxis.jogPositivePressed()   = StartJog(axis)
///   右摇杆回中（Forward Released） → currentAxis.jogPositiveReleased()  = StopJog(axis)
///   右摇杆向下（Backward Pressed）  → currentAxis.jogNegativePressed()   = StartJog(axis, Backward)
///   右摇杆回中（Backward Released） → currentAxis.jogNegativeReleased()  = StopJog(axis)
///
/// 跨轴跳跃保护：
///   当用户在摇杆推动期间切换轴时，先对旧轴发送 Released，再对新轴发送 Pressed
class MotionController : public QObject
{
    Q_OBJECT

public:
    /// @param interpreter  摇杆事件源（emit inputEvent）
    /// @param axisModel    当前选轴模型（emit currentAxisChanged）
    explicit MotionController(GamepadInputInterpreter* interpreter,
                              AxisSelectionModel* axisModel,
                              QObject* parent = nullptr);

    /// @brief 注册轴 → ViewModel 映射（供 onInputEvent 查找对应 ViewModel 发送 jog 指令）
    void registerAxis(AxisId id, QtAxisViewModel* vm);

public slots:
    /// @brief 消费 Motion 类型的 InputEvent
    void onInputEvent(const InputEvent& event);

    /// @brief 轴切换时：先释放旧轴的活跃 jog，再对新轴重放当前摇杆方向
    void onCurrentAxisChanged(AxisId newAxis);

private:
    /// @brief 释放当前轴的活跃 jog（给旧轴发 Released）
    void releaseCurrentMotion();

    /// @brief 对当前轴发起指定方向的 jog（jogPositivePressed 或 jogNegativePressed）
    void pressMotion(MotionDirection dir);

    AxisSelectionModel* m_axisModel;
    std::unordered_map<AxisId, QtAxisViewModel*> m_vmMap;

    AxisId m_currentAxis = AxisId::Y;

    // 记录当前活跃方向（上一次 Non-Neutral 的方向），用于轴切换时"重放"
    MotionDirection m_activeMotionDir = MotionDirection::Forward;  // 任意初始值，会在首次事件时覆盖
    bool m_motionActive = false;  // true = 摇杆当前在死区外
};