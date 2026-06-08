#include "MotionController.h"
#include "presentation/viewmodel/QtAxisViewModel.h"
#include "AxisSelectionModel.h"
#include "GamepadInputInterpreter.h"
#include <QDebug>

MotionController::MotionController(GamepadInputInterpreter* interpreter,
                                   AxisSelectionModel* axisModel,
                                   QObject* parent)
    : QObject(parent)
    , m_axisModel(axisModel)
{
    // 1. 消费摇杆 Motion 事件
    bool ok1 = connect(interpreter, &GamepadInputInterpreter::inputEvent,
                       this, &MotionController::onInputEvent);
    qDebug() << "[MotionCtrl] connect(interpreter::inputEvent -> onInputEvent) returned" << ok1;

    // 2. 监听轴切换 → 跨轴跳跃保护
    bool ok2 = connect(axisModel, &AxisSelectionModel::currentAxisChanged,
                       this, &MotionController::onCurrentAxisChanged);
    qDebug() << "[MotionCtrl] connect(axisModel::currentAxisChanged -> onCurrentAxisChanged) returned" << ok2;

    if (!ok1 || !ok2) {
        qWarning() << "[MotionCtrl] ❌ Signal-slot connection FAILED! Motion control will not work.";
    } else {
        qDebug() << "[MotionCtrl] ✅ Ready. Listening for Motion events on currentAxis="
                 << axisModel->currentAxisName();
    }
}

void MotionController::registerAxis(AxisId id, QtAxisViewModel* vm)
{
    m_vmMap[id] = vm;
    qDebug() << "[MotionCtrl] registered axis:" << static_cast<int>(id) << "→ VM =" << vm;
}

// ═══════════════════════════════════════════════════════
// onInputEvent: 消费 Motion 类型的 InputEvent
//   右摇杆向上（Forward）  → jogPositivePressed()
//   右摇杆向下（Backward） → jogNegativePressed()
//   回中（Released）      → 对应的 Released()
// ═══════════════════════════════════════════════════════
void MotionController::onInputEvent(const InputEvent& event)
{
    if (event.type != InputEvent::Type::Motion) {
        return;  // 非 Motion 事件，忽略
    }

    qDebug() << "[MotionCtrl] onInputEvent  motionDir=" << static_cast<int>(event.motionDir)
             << " motionType=" << static_cast<int>(event.motionType)
             << " currentAxis=" << static_cast<int>(m_currentAxis);

    auto it = m_vmMap.find(m_currentAxis);
    if (it == m_vmMap.end()) {
        qDebug() << "[MotionCtrl] ❌ No ViewModel registered for axis:" << static_cast<int>(m_currentAxis);
        return;
    }
    QtAxisViewModel* vm = it->second;

    if (event.motionType == MotionEventType::Pressed) {
        // ── 按下 ──
        m_motionActive = true;
        m_activeMotionDir = event.motionDir;

        if (event.motionDir == MotionDirection::Forward) {
            qDebug() << "[MotionCtrl] 🎮 StartJog(" << m_axisModel->currentAxisName() << ") Forward";
            vm->jogPositivePressed();
        } else {
            qDebug() << "[MotionCtrl] 🎮 StartJog(" << m_axisModel->currentAxisName() << ") Backward";
            vm->jogNegativePressed();
        }
    } else {
        // ── 释放（回中）──
        m_motionActive = false;

        if (event.motionDir == MotionDirection::Forward) {
            qDebug() << "[MotionCtrl] 🛑 StopJog(" << m_axisModel->currentAxisName() << ") Forward";
            vm->jogPositiveReleased();
        } else {
            qDebug() << "[MotionCtrl] 🛑 StopJog(" << m_axisModel->currentAxisName() << ") Backward";
            vm->jogNegativeReleased();
        }
    }
}

// ═══════════════════════════════════════════════════════
// onCurrentAxisChanged: 跨轴跳跃保护
//   摇杆正在推着（m_motionActive == true）时用户切换轴：
//     1. 对旧轴发送 Released
//     2. 更新 m_currentAxis
//     3. 对新轴重放 Pressed
// ═══════════════════════════════════════════════════════
void MotionController::onCurrentAxisChanged(AxisId newAxis)
{
    qDebug() << "[MotionCtrl] onCurrentAxisChanged  old=" << static_cast<int>(m_currentAxis)
             << "→ new=" << static_cast<int>(newAxis)
             << " motionActive=" << m_motionActive;

    if (m_motionActive) {
        // 摇杆正在推动中，需要做"跨轴跳跃保护"
        // Step 1: 释放旧轴
        releaseCurrentMotion();

        // Step 2: 更新当前轴
        m_currentAxis = newAxis;

        // Step 3: 对新轴重放当前方向
        pressMotion(m_activeMotionDir);
    } else {
        // 摇杆在中位，只是更新记录
        m_currentAxis = newAxis;
    }
}

void MotionController::releaseCurrentMotion()
{
    auto it = m_vmMap.find(m_currentAxis);
    if (it == m_vmMap.end()) return;
    QtAxisViewModel* vm = it->second;

    if (m_activeMotionDir == MotionDirection::Forward) {
        qDebug() << "[MotionCtrl] 🔀 cross-axis release: StopJog("
                 << QString::fromLatin1(axisIdToString(m_currentAxis)) << ") Forward";
        vm->jogPositiveReleased();
    } else {
        qDebug() << "[MotionCtrl] 🔀 cross-axis release: StopJog("
                 << QString::fromLatin1(axisIdToString(m_currentAxis)) << ") Backward";
        vm->jogNegativeReleased();
    }
}

void MotionController::pressMotion(MotionDirection dir)
{
    auto it = m_vmMap.find(m_currentAxis);
    if (it == m_vmMap.end()) return;
    QtAxisViewModel* vm = it->second;

    if (dir == MotionDirection::Forward) {
        qDebug() << "[MotionCtrl] 🔀 cross-axis press: StartJog("
                 << QString::fromLatin1(axisIdToString(m_currentAxis)) << ") Forward";
        vm->jogPositivePressed();
    } else {
        qDebug() << "[MotionCtrl] 🔀 cross-axis press: StartJog("
                 << QString::fromLatin1(axisIdToString(m_currentAxis)) << ") Backward";
        vm->jogNegativePressed();
    }
}