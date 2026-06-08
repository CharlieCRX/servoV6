#pragma once

#include <QObject>
#include "InputEvent.h"

class AxisSelectionModel;
class GamepadInputInterpreter;

/// @brief 消费 AxisSelect InputEvent，驱动 AxisSelectionModel
/// 职责极窄：只处理 event.type == AxisSelect
class AxisSelectionController : public QObject
{
    Q_OBJECT

public:
    /// @brief 构造函数自动连接 interpreter → this → model 信号链
    explicit AxisSelectionController(GamepadInputInterpreter* interpreter,
                                     AxisSelectionModel* model,
                                     QObject* parent = nullptr);

public slots:
    /// @brief 接收来自 GamepadInputInterpreter 的统一 InputEvent
    void onInputEvent(const InputEvent& event);

private:
    AxisSelectionModel* m_model;
};
