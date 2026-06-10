#include "AxisSelectionController.h"
#include "AxisSelectionModel.h"
#include "GamepadInputInterpreter.h"
#include "presentation/input/MotionController.h"
#include <QDebug>

AxisSelectionController::AxisSelectionController(GamepadInputInterpreter* interpreter,
                                                 AxisSelectionModel* model,
                                                 QObject* parent)
    : QObject(parent)
    , m_model(model)
{
    // 连接信号链：Interpreter::inputEvent → Controller::onInputEvent → Model::selectLeft/Right
    bool ok = connect(interpreter, &GamepadInputInterpreter::inputEvent,
                      this, &AxisSelectionController::onInputEvent);
    qDebug() << "[AxisCtrl] connect(interpreter::inputEvent -> onInputEvent) returned" << ok;
    if (!ok) {
        qWarning() << "[AxisCtrl] ❌ Signal-slot connection FAILED! No axis events will reach the model.";
    }
}

void AxisSelectionController::onInputEvent(const InputEvent& event)
{
    qDebug() << "[AxisCtrl] onInputEvent type=" << static_cast<int>(event.type);
    if (event.type != InputEvent::Type::AxisSelect) {
        qDebug() << "[AxisCtrl] ignoring: not AxisSelect (type=" << static_cast<int>(event.type) << ")";
        return;
    }

    // ★ JOG 点动活跃时（摇杆正在推），阻止左摇杆切换轴
    if (m_motionCtrl && m_motionCtrl->jogActiveDirection() != 0) {
        qDebug() << "[AxisCtrl] 🚫 axis select blocked: JOG is active (direction="
                 << m_motionCtrl->jogActiveDirection() << ")";
        return;
    }

    if (event.axisDir == AxisSelectDirection::Left) {
        qDebug() << "[AxisCtrl] → Left → calling axisModel->selectLeft()";
        m_model->selectLeft();
    } else {
        qDebug() << "[AxisCtrl] → Right → calling axisModel->selectRight()";
        m_model->selectRight();
    }
}
