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

    // ★ 顺序约束：不允许直接在 ±1 之间跳转，必须经过 0
    //   前进 → 后退：必须先松开前进 (0) 才能按后退 (-1)
    //   这确保一个方向的点动完全停止后，才能启动另一个方向
    if (m_jogActiveDirection != 0 && dir != 0) {
        qDebug() << "[MotionCtrl] 🚫 jogActiveDirection blocked: cannot jump from "
                 << m_jogActiveDirection << " to " << dir << " (must go through 0)";
        return;
    }

    // ★ 查找当前轴的 ViewModel
    auto it = m_vmMap.find(m_currentAxis);
    QtAxisViewModel* vm = (it != m_vmMap.end()) ? it->second : nullptr;

    // ★ 先停止旧方向的 JOG（如果从 ±1 → 0）
    if (vm) {
        if (m_jogActiveDirection == 1) {
            qDebug() << "[MotionCtrl] 🛑 setJogActiveDirection: releasing Forward JOG";
            vm->jogPositiveReleased();
        } else if (m_jogActiveDirection == -1) {
            qDebug() << "[MotionCtrl] 🛑 setJogActiveDirection: releasing Backward JOG";
            vm->jogNegativeReleased();
        }
    }

    m_jogActiveDirection = dir;

    // ★ 再启动新方向的 JOG（如果需要）
    if (vm) {
        if (dir == 1) {
            qDebug() << "[MotionCtrl] 🎮 setJogActiveDirection: starting Forward JOG";
            vm->jogPositivePressed();
        } else if (dir == -1) {
            qDebug() << "[MotionCtrl] 🎮 setJogActiveDirection: starting Backward JOG";
            vm->jogNegativePressed();
        }
        // dir == 0: 仅停止，不启动
    }

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
    if (event.motionType == MotionEventType::Pressed) {
        m_motionActive = true;
        m_activeMotionDir = event.motionDir;

        int targetDir = (event.motionDir == MotionDirection::Forward) ? 1 : -1;

        // ★ 快速摆动检测：上一次 Released 在阈值时间内，且当前方向已归零
        //   摇杆从 Forward 甩到 Backward 时，解释器在同一帧 emit ForwardReleased + BackwardPressed
        //   第二次进入时 m_jogActiveDirection==0（刚被 Released 清零），且计时器刚启动
        if (m_jogReleaseTimer.isValid()) {
            qint64 elapsed = m_jogReleaseTimer.elapsed();
            if (elapsed < kRapidWiggleThresholdMs && m_jogActiveDirection == 0) {
                qDebug() << "[MotionCtrl] 🚫 rapid wiggle detected (elapsed=" << elapsed
                         << "ms) — cancelling JOG, not starting direction " << targetDir;
                m_jogReleaseTimer.invalidate();
                return;  // 拒绝启动新方向，轴停在当前位置
            }
        }
        m_jogReleaseTimer.invalidate();  // 正常操作，清除计时器

        setJogActiveDirection(targetDir);
    } else {
        m_motionActive = false;
        setJogActiveDirection(0);
        m_jogReleaseTimer.start();  // ★ 记录释放时刻，用于快速摆动检测
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
    qDebug() << "[MotionCtrl] 🔀 cross-axis release: axis="
             << QString::fromLatin1(axisIdToString(m_currentAxis));
    // ★ 由 setJogActiveDirection 统一处理：停止当前方向的 JOG
    setJogActiveDirection(0);
}

void MotionController::pressMotion(MotionDirection dir)
{
    qDebug() << "[MotionCtrl] 🔀 cross-axis press: axis="
             << QString::fromLatin1(axisIdToString(m_currentAxis))
             << " dir=" << (dir == MotionDirection::Forward ? "Forward" : "Backward");
    // ★ 由 setJogActiveDirection 统一处理：停止旧方向 → 启动新方向
    if (dir == MotionDirection::Forward) {
        setJogActiveDirection(1);
    } else {
        setJogActiveDirection(-1);
    }
}
