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
    const bool ok = connect(interpreter, &GamepadInputInterpreter::inputEvent,
                            this, &AxisSelectionController::onInputEvent);
    qDebug() << "[AxisCtrl] connect inputEvent -> onInputEvent:" << ok;
    if (!ok) {
        qWarning() << "[AxisCtrl] Signal-slot connection failed; axis events will not reach model";
    }
}

void AxisSelectionController::onInputEvent(const InputEvent& event)
{
    qDebug() << "[AxisCtrl] onInputEvent type=" << static_cast<int>(event.type);
    if (event.type != InputEvent::Type::AxisSelect) {
        qDebug() << "[AxisCtrl] ignoring non-axis event type=" << static_cast<int>(event.type);
        return;
    }

    if (m_motionCtrl && m_motionCtrl->jogActiveDirection() != 0) {
        qDebug() << "[AxisCtrl] axis select blocked: JOG active direction="
                 << m_motionCtrl->jogActiveDirection();
        return;
    }

    if (event.axisDir == AxisSelectDirection::Left) {
        qDebug() << "[AxisCtrl] selecting left";
        m_model->selectLeft();
    } else {
        qDebug() << "[AxisCtrl] selecting right";
        m_model->selectRight();
    }
}
