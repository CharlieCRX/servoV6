#include "AxisSelectionController.h"
#include "AxisSelectionModel.h"
#include "GamepadInputInterpreter.h"

AxisSelectionController::AxisSelectionController(GamepadInputInterpreter* interpreter,
                                                 AxisSelectionModel* model,
                                                 QObject* parent)
    : QObject(parent)
    , m_model(model)
{
    // 连接信号链：Interpreter::inputEvent → Controller::onInputEvent → Model::selectLeft/Right
    connect(interpreter, &GamepadInputInterpreter::inputEvent,
            this, &AxisSelectionController::onInputEvent);
}

void AxisSelectionController::onInputEvent(const InputEvent& event)
{
    if (event.type != InputEvent::Type::AxisSelect) return;

    if (event.axisDir == AxisSelectDirection::Left) {
        m_model->selectLeft();
    } else {
        m_model->selectRight();
    }
}
