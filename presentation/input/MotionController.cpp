#include "MotionController.h"
#include "AxisSelectionModel.h"
#include "GamepadInputInterpreter.h"
#include "presentation/input/JoystickCommandBuilder.h"
#include "application_vnext/control/MotionControlService.h"
#include <QDebug>

using namespace presentation::input::joystick;

MotionController::MotionController(GamepadInputInterpreter* interpreter,
                                   AxisSelectionModel* axisModel,
                                   application_vnext::control::MotionControlService* service,
                                   QObject* parent)
    : QObject(parent)
    , m_axisModel(axisModel)
    , m_service(service)
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
                 << axisModel->currentAxisName()
                 << (m_service ? "（已接入 MotionControlService）" : "（未接入 service，仅本地模式/视觉反馈）");
    }
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

    // ★ 提交到统一协调层（Phase 4）：先停旧方向（±1 → 0），再启动新方向（0 → ±1）。
    //   一律构造 Joystick 源业务意图命令，绝不在摇杆侧写 PLC。
    const auto target = currentAxisTarget();

    if (m_jogActiveDirection == 1 || m_jogActiveDirection == -1) {
        qDebug() << "[MotionCtrl] 🛑 setJogActiveDirection: submitting StopJog";
        submit(makeStopJogCommand(target));
    }

    m_jogActiveDirection = dir;

    if (dir == 1) {
        qDebug() << "[MotionCtrl] 🎮 setJogActiveDirection: submitting StartJogForward";
        submit(makeJogForwardCommand(target));
    } else if (dir == -1) {
        qDebug() << "[MotionCtrl] 🎮 setJogActiveDirection: submitting StartJogBackward";
        submit(makeJogBackwardCommand(target));
    }
    // dir == 0: 仅停止，不启动

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

    // 释放（回中）：触发定位移动（走统一协调层，Phase 4）
    m_motionActive = false;

    const auto target = currentAxisTarget();
    const QString axisName = m_axisModel->currentAxisName();

    // 从统一快照读取同一轴的定位目标预填值 + 定位速度，原子携带到 Start*Move（§5.3）。
    // 三条件才提交定位：① 快照中找到该轴；② 轴已绑定（bound）且反馈可信（trusted）；
    // ③ 定位速度为正（speed>0，绝不写 0 覆盖 PLC 速度）。任一不满足 → 记录诊断并跳过，
    // 由协调层 execute() 的权威校验兜底，保证「任何来源都无法写 0 速度」。
    bool axisOk  = false;   // 快照中找到目标轴
    bool ready   = false;   // 绑定且可信
    float absTarget = 0.0f;
    float speed     = 0.0f;
    if (m_service) {
        for (const auto& a : m_service->store().snapshot().axes) {
            if (a.group == target.group && a.role == target.function) {
                axisOk    = true;
                ready     = a.bound && a.trusted;
                absTarget = a.absMoveTarget;
                speed     = a.positioningSpeed;
                break;
            }
        }
    }

    if (!axisOk) {
        qWarning() << "[MotionCtrl] ⚠️ 定位跳过：快照未找到目标轴" << axisName
                   << "，不提交 Start*Move";
        return;
    }
    if (!ready) {
        qWarning() << "[MotionCtrl] ⚠️ 定位跳过：轴" << axisName
                   << "未绑定或反馈不可信，不提交 Start*Move";
        return;
    }
    if (speed <= 0.0f) {
        qWarning() << "[MotionCtrl] ⚠️ 定位跳过：轴" << axisName
                   << "定位速度不合法（speed=" << speed << "），不提交 Start*Move";
        return;
    }

    if (m_isAbsolute) {
        qDebug() << "[MotionCtrl] 📍 Position Released → StartAbsMove  axis=" << axisName
                 << " target=" << absTarget << " speed=" << speed;
        submit(makePositionCommand(target, /*abs=*/true, absTarget, speed));
    } else {
        const float step = static_cast<float>(m_relStep);
        qDebug() << "[MotionCtrl] 📍 Position Released → StartRelMove  axis=" << axisName
                 << " step=" << step << " speed=" << speed;
        submit(makePositionCommand(target, /*abs=*/false, step, speed));
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
        // 跨轴跳跃保护：先对旧轴提交 StopJog，再对新轴重放当前摇杆方向
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
    // ★ 对当前（旧）轴提交 StopJog，由 setJogActiveDirection 统一处理
    setJogActiveDirection(0);
}

void MotionController::pressMotion(MotionDirection dir)
{
    qDebug() << "[MotionCtrl] 🔀 cross-axis press: axis="
             << QString::fromLatin1(axisIdToString(m_currentAxis))
             << " dir=" << (dir == MotionDirection::Forward ? "Forward" : "Backward");
    // ★ 对新轴重放当前方向：停止旧方向 → 启动新方向（经统一协调层）
    if (dir == MotionDirection::Forward) {
        setJogActiveDirection(1);
    } else {
        setJogActiveDirection(-1);
    }
}

application_vnext::control::AxisTarget MotionController::currentAxisTarget() const
{
    application_vnext::control::AxisTarget t;
    t.group = joystickGroup();  // 摇杆恒为 A 组（AxisSelectionModel 单组模型）
    switch (m_currentAxis) {
        case AxisId::Y:  t.function = domain_vnext::model::AxisFunction::Y;  break;
        case AxisId::Z:  t.function = domain_vnext::model::AxisFunction::Z;  break;
        case AxisId::R:  t.function = domain_vnext::model::AxisFunction::R;  break;
        case AxisId::X:  t.function = domain_vnext::model::AxisFunction::X;  break;
        case AxisId::X1: t.function = domain_vnext::model::AxisFunction::X1; break;
        case AxisId::X2: t.function = domain_vnext::model::AxisFunction::X2; break;
    }
    return t;
}

void MotionController::submit(application_vnext::control::ControlCommand cmd)
{
    if (!m_service) {
        qDebug() << "[MotionCtrl]（未接入 MotionControlService）跳过提交"
                 << controlActionName(cmd.action);
        return;
    }
    cmd.operationId = m_service->submit(std::move(cmd));
    qDebug() << "[MotionCtrl] ✅ 已提交"
             << controlActionName(cmd.action)
             << "axis=" << QString::fromStdString(
                    application_vnext::control::axisTargetName(
                        currentAxisTarget()))
             << "op=" << QString::fromStdString(cmd.operationId);
}
