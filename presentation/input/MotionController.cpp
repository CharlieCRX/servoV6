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

void MotionController::setControlMode(int mode)
{
    if (m_controlMode == mode) return;

    // 切换模式前：如果正在 JOG 活跃中，先释放
    if (m_controlMode == 0 && m_motionActive) {
        qDebug() << "[MotionCtrl] 🛑 mode switch: releasing active JOG first on axis="
                 << static_cast<int>(m_currentAxis);
        releaseCurrentMotion();
        m_motionActive = false;
    }

    m_controlMode = mode;

    qDebug() << "[MotionCtrl] controlMode changed to" << (mode == 0 ? "JOG" : "Position");
    emit controlModeChanged();
}

void MotionController::setIsAbsolute(bool absolute)
{
    if (m_isAbsolute == absolute) return;
    m_isAbsolute = absolute;
    qDebug() << "[MotionCtrl] isAbsolute changed to" << (absolute ? "Absolute" : "Relative");
    emit isAbsoluteChanged();
}

void MotionController::setJogActiveDirection(int dir)
{
    // 仅在 0/1/-1 范围内接受
    if (dir < -1 || dir > 1) return;
    if (m_jogActiveDirection == dir) return;
    m_jogActiveDirection = dir;
    qDebug() << "[MotionCtrl] jogActiveDirection changed to" << dir;
    emit jogActiveDirectionChanged();
}

void MotionController::toggleMode()
{
    int newMode = (m_controlMode == 0) ? 1 : 0;
    setControlMode(newMode);
}

// ═══════════════════════════════════════════════════════
// onInputEvent: 消费 Motion 类型的 InputEvent
//   根据控制模式分派到 handleJogMotion 或 handlePositionMotion
// ═══════════════════════════════════════════════════════
void MotionController::onInputEvent(const InputEvent& event)
{
    if (event.type != InputEvent::Type::Motion) {
        return;
    }

    qDebug() << "[MotionCtrl] onInputEvent  motionDir=" << static_cast<int>(event.motionDir)
             << " motionType=" << static_cast<int>(event.motionType)
             << " currentAxis=" << static_cast<int>(m_currentAxis)
             << " mode=" << (m_controlMode == 0 ? "JOG" : "Position");

    if (m_controlMode == 0) {
        handleJogMotion(event);
    } else {
        handlePositionMotion(event);
    }
}

// ═══════════════════════════════════════════════════════
// handleJogMotion: JOG 模式（原有逻辑不变）
// ═══════════════════════════════════════════════════════
void MotionController::handleJogMotion(const InputEvent& event)
{
    auto it = m_vmMap.find(m_currentAxis);
    if (it == m_vmMap.end()) {
        qDebug() << "[MotionCtrl] ❌ No ViewModel registered for axis:" << static_cast<int>(m_currentAxis);
        return;
    }
    QtAxisViewModel* vm = it->second;

    if (event.motionType == MotionEventType::Pressed) {
        m_motionActive = true;
        m_activeMotionDir = event.motionDir;

        if (event.motionDir == MotionDirection::Forward) {
            qDebug() << "[MotionCtrl] 🎮 StartJog(" << m_axisModel->currentAxisName() << ") Forward";
            vm->jogPositivePressed();
            setJogActiveDirection(1);       // ★ QML "前进 +" 按钮高亮
        } else {
            qDebug() << "[MotionCtrl] 🎮 StartJog(" << m_axisModel->currentAxisName() << ") Backward";
            vm->jogNegativePressed();
            setJogActiveDirection(-1);      // ★ QML "后退 -" 按钮高亮
        }
    } else {
        m_motionActive = false;

        if (event.motionDir == MotionDirection::Forward) {
            qDebug() << "[MotionCtrl] 🛑 StopJog(" << m_axisModel->currentAxisName() << ") Forward";
            vm->jogPositiveReleased();
        } else {
            qDebug() << "[MotionCtrl] 🛑 StopJog(" << m_axisModel->currentAxisName() << ") Backward";
            vm->jogNegativeReleased();
        }
        setJogActiveDirection(0);           // ★ 取消按钮高亮
    }
}

// ═══════════════════════════════════════════════════════
// handlePositionMotion: Position 模式处理
//   Pressed  → 不做任何操作
//   Released → 触发定位移动（目标值由用户在 UI 上预设）
//     绝对子模式 → triggerAbsMove()
//     相对子模式 → triggerRelMove()
// ═══════════════════════════════════════════════════════
void MotionController::handlePositionMotion(const InputEvent& event)
{
    if (event.motionType == MotionEventType::Pressed) {
        // 按下：不做任何操作
        m_motionActive = true;
        qDebug() << "[MotionCtrl] 📍 Position Pressed (no action)";
        return;
    }

    // 释放（回中）：触发位置移动
    m_motionActive = false;

    auto it = m_vmMap.find(m_currentAxis);
    if (it == m_vmMap.end()) {
        qDebug() << "[MotionCtrl] ❌ No ViewModel registered for axis:" << static_cast<int>(m_currentAxis);
        return;
    }
    QtAxisViewModel* vm = it->second;

    QString axisName = m_axisModel->currentAxisName();

    if (m_isAbsolute) {
        qDebug() << "[MotionCtrl] 📍 Position Released → triggerAbsMove()  axis=" << axisName;
        vm->triggerAbsMove();
    } else {
        qDebug() << "[MotionCtrl] 📍 Position Released → triggerRelMove()  axis=" << axisName;
        vm->triggerRelMove();
    }
}

// ═══════════════════════════════════════════════════════
// onCurrentAxisChanged: 跨轴跳跃保护
// ═══════════════════════════════════════════════════════
void MotionController::onCurrentAxisChanged(AxisId newAxis)
{
    qDebug() << "[MotionCtrl] onCurrentAxisChanged  old=" << static_cast<int>(m_currentAxis)
             << "→ new=" << static_cast<int>(newAxis)
             << " motionActive=" << m_motionActive
             << " mode=" << (m_controlMode == 0 ? "JOG" : "Position");

    if (m_motionActive && m_controlMode == 0) {
        releaseCurrentMotion();
        m_currentAxis = newAxis;
        pressMotion(m_activeMotionDir);
    } else {
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
    setJogActiveDirection(0);  // ★ 跨轴切换时先清除视觉
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
        setJogActiveDirection(1);       // ★ 新轴：高亮前进按钮
    } else {
        qDebug() << "[MotionCtrl] 🔀 cross-axis press: StartJog("
                 << QString::fromLatin1(axisIdToString(m_currentAxis)) << ") Backward";
        vm->jogNegativePressed();
        setJogActiveDirection(-1);      // ★ 新轴：高亮后退按钮
    }
}
