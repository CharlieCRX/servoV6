#include "MotionController.h"

#include "AxisSelectionModel.h"
#include "GamepadInputInterpreter.h"
#include "application_vnext/control/MotionControlService.h"
#include "infrastructure/logger/Logger.h"
#include "presentation/input/JoystickCommandBuilder.h"

#include <QDebug>

#include <utility>

using namespace presentation::input::joystick;

MotionController::MotionController(GamepadInputInterpreter* interpreter,
                                   AxisSelectionModel* axisModel,
                                   application_vnext::control::MotionControlService* service,
                                   QObject* parent)
    : QObject(parent)
    , m_axisModel(axisModel)
    , m_service(service)
    , m_currentAxis(axisModel ? axisModel->currentAxis() : AxisId::Y)
{
    const bool ok1 = connect(interpreter, &GamepadInputInterpreter::inputEvent, 
                             this, &MotionController::onInputEvent);
    qDebug() << "[MotionCtrl] connect inputEvent -> onInputEvent:" << ok1;

    const bool ok2 = connect(axisModel, &AxisSelectionModel::currentAxisChanged,
                             this, &MotionController::onCurrentAxisChanged);
    qDebug() << "[MotionCtrl] connect currentAxisChanged -> onCurrentAxisChanged:" << ok2;

    const bool ok3 = connect(axisModel, &AxisSelectionModel::currentGroupChanged,
                             this, &MotionController::onCurrentGroupChanged);
    qDebug() << "[MotionCtrl] connect currentGroupChanged -> onCurrentGroupChanged:" << ok3;

    if (!ok1 || !ok2 || !ok3) {
        qWarning() << "[MotionCtrl] Signal-slot connection failed; motion control is disabled";
    } else {
        qDebug() << "[MotionCtrl] Ready. currentAxis=" << axisModel->currentAxisName()
                 << "service=" << (m_service ? "connected" : "not connected");
    }
}

void MotionController::setControlMode(int mode)
{
    if (m_controlMode == mode) return;

    if (m_controlMode == 0 && m_motionActive) {
        qDebug() << "[MotionCtrl] mode switch: releasing active JOG first. axis="
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
    if (dir < -1 || dir > 1) return;
    if (m_jogActiveDirection == dir) return;

    if (m_jogActiveDirection != 0 && dir != 0) {
        qDebug() << "[MotionCtrl] jogActiveDirection blocked: cannot jump from"
                 << m_jogActiveDirection << "to" << dir << "(must pass through 0)";
        return;
    }

    const auto target = currentAxisTarget();

    if (m_jogActiveDirection == 1 || m_jogActiveDirection == -1) {
        stopActiveJog("setJogActiveDirection");
        if (dir == 0) {
            qDebug() << "[MotionCtrl] jogActiveDirection changed to" << dir;
            return;
        }
    }

    if (dir == 1) {
        startJogForTarget(target, dir, "setJogActiveDirection");
    } else if (dir == -1) {
        startJogForTarget(target, dir, "setJogActiveDirection");
    } else {
        m_jogActiveDirection = 0;
        m_activeJogTarget.reset();
        emit jogActiveDirectionChanged();
    }

    qDebug() << "[MotionCtrl] jogActiveDirection changed to" << dir;
}

void MotionController::toggleMode()
{
    setControlMode(m_controlMode == 0 ? 1 : 0);
}

void MotionController::onInputEvent(const InputEvent& event)
{
    if (event.type != InputEvent::Type::Motion) return;

    qDebug() << "[MotionCtrl] onInputEvent motionDir=" << static_cast<int>(event.motionDir)
             << "motionType=" << static_cast<int>(event.motionType)
             << "currentAxis=" << static_cast<int>(m_currentAxis)
             << "mode=" << (m_controlMode == 0 ? "JOG" : "Position");

    if (m_controlMode == 0) {
        handleJogMotion(event);
    } else {
        handlePositionMotion(event);
    }
}

void MotionController::handleJogMotion(const InputEvent& event)
{
    if (event.motionType == MotionEventType::Pressed) {
        m_motionActive = true;
        m_activeMotionDir = event.motionDir;

        const int targetDir = (event.motionDir == MotionDirection::Forward) ? 1 : -1;

        if (m_jogReleaseTimer.isValid()) {
            const qint64 elapsed = m_jogReleaseTimer.elapsed();
            if (elapsed < kRapidWiggleThresholdMs && m_jogActiveDirection == 0) {
                qDebug() << "[MotionCtrl] rapid wiggle detected. elapsedMs=" << elapsed
                         << "cancelledDirection=" << targetDir;
                m_jogReleaseTimer.invalidate();
                return;
            }
        }
        m_jogReleaseTimer.invalidate();

        setJogActiveDirection(targetDir);
    } else {
        m_motionActive = false;
        setJogActiveDirection(0);
        m_jogReleaseTimer.start();
    }
}

void MotionController::handlePositionMotion(const InputEvent& event)
{
    if (event.motionType == MotionEventType::Pressed) {
        m_motionActive = true;
        qDebug() << "[MotionCtrl] Position pressed; no action";
        return;
    }

    m_motionActive = false;

    const auto target = currentAxisTarget();
    const QString axisName = m_axisModel->currentAxisName();

    bool axisOk = false;
    bool ready = false;
    float absTarget = 0.0f;
    float speed = 0.0f;
    if (m_service) {
        for (const auto& a : m_service->store().snapshot().axes) {
            if (a.group == target.group && a.role == target.function) {
                axisOk = true;
                ready = a.bound && a.trusted;
                absTarget = a.absMoveTarget;
                speed = a.positioningSpeed;
                break;
            }
        }
    }

    if (!axisOk) {
        qWarning() << "[MotionCtrl] Position skipped: target axis not found in snapshot. axis="
                   << axisName;
        return;
    }
    if (!ready) {
        qWarning() << "[MotionCtrl] Position skipped: target axis is unbound or untrusted. axis="
                   << axisName;
        return;
    }
    if (speed <= 0.0f) {
        qWarning() << "[MotionCtrl] Position skipped: non-positive speed. axis="
                   << axisName << "speed=" << speed;
        return;
    }

    if (m_isAbsolute) {
        qDebug() << "[MotionCtrl] Position released: submitting StartAbsMove axis="
                 << axisName << "target=" << absTarget << "speed=" << speed;
        submit(makePositionCommand(target, /*abs=*/true, absTarget, speed));
    } else {
        const float step = static_cast<float>(m_relStep);
        qDebug() << "[MotionCtrl] Position released: submitting StartRelMove axis="
                 << axisName << "step=" << step << "speed=" << speed;
        submit(makePositionCommand(target, /*abs=*/false, step, speed));
    }
}

void MotionController::onCurrentAxisChanged(AxisId newAxis)
{
    qDebug() << "[MotionCtrl] onCurrentAxisChanged old=" << static_cast<int>(m_currentAxis)
             << "new=" << static_cast<int>(newAxis)
             << "motionActive=" << m_motionActive
             << "mode=" << (m_controlMode == 0 ? "JOG" : "Position");

    if (m_motionActive && m_controlMode == 0) {
        m_currentAxis = newAxis;
        retargetActiveJog("axis changed");
    } else {
        m_currentAxis = newAxis;
    }
}

void MotionController::onCurrentGroupChanged()
{
    const QString groupName = m_axisModel ? m_axisModel->currentGroupName()
                                          : QStringLiteral("?");
    qDebug() << "[MotionCtrl] onCurrentGroupChanged group=" << groupName
             << "motionActive=" << m_motionActive
             << "mode=" << (m_controlMode == 0 ? "JOG" : "Position")
             << "dir=" << m_jogActiveDirection;

    LOG_INFO(LogLayer::UI, "MotionCtrl",
             "currentGroupChanged group=" + groupName.toStdString()
             + " motionActive=" + std::to_string(m_motionActive)
             + " mode=" + std::string(m_controlMode == 0 ? "JOG" : "Position")
             + " jogDir=" + std::to_string(m_jogActiveDirection));

    if (m_motionActive && m_controlMode == 0) {
        retargetActiveJog("group changed");
    }
}

void MotionController::releaseCurrentMotion()
{
    qDebug() << "[MotionCtrl] cross-axis release: axis="
             << QString::fromLatin1(axisIdToString(m_currentAxis));
    setJogActiveDirection(0);
}

void MotionController::pressMotion(MotionDirection dir)
{
    qDebug() << "[MotionCtrl] cross-axis press: axis="
             << QString::fromLatin1(axisIdToString(m_currentAxis))
             << "dir=" << (dir == MotionDirection::Forward ? "Forward" : "Backward");

    setJogActiveDirection(dir == MotionDirection::Forward ? 1 : -1);
}

void MotionController::stopActiveJog(const char* reason)
{
    if (m_jogActiveDirection == 0) return;

    const auto target = m_activeJogTarget.value_or(currentAxisTarget());
    qDebug() << "[MotionCtrl] stopActiveJog reason=" << reason
             << "axis=" << QString::fromStdString(application_vnext::control::axisTargetName(target));
    LOG_INFO(LogLayer::UI, "MotionCtrl",
             "stopActiveJog reason=" + std::string(reason ? reason : "")
             + " target=" + application_vnext::control::axisTargetName(target));

    submit(makeStopJogCommand(target));
    m_activeJogTarget.reset();
    m_jogActiveDirection = 0;
    emit jogActiveDirectionChanged();
}

void MotionController::startJogForTarget(
    const application_vnext::control::AxisTarget& target,
    int dir,
    const char* reason)
{
    if (dir == 1) {
        qDebug() << "[MotionCtrl] startJogForTarget: submitting StartJogForward reason="
                 << reason
                 << "axis=" << QString::fromStdString(application_vnext::control::axisTargetName(target));
        submit(makeJogForwardCommand(target));
    } else if (dir == -1) {
        qDebug() << "[MotionCtrl] startJogForTarget: submitting StartJogBackward reason="
                 << reason
                 << "axis=" << QString::fromStdString(application_vnext::control::axisTargetName(target));
        submit(makeJogBackwardCommand(target));
    } else {
        return;
    }

    LOG_INFO(LogLayer::UI, "MotionCtrl",
             "startJog reason=" + std::string(reason ? reason : "")
             + " target=" + application_vnext::control::axisTargetName(target)
             + " dir=" + std::to_string(dir));
    m_activeJogTarget = target;
    m_jogActiveDirection = dir;
    emit jogActiveDirectionChanged();
}

void MotionController::retargetActiveJog(const char* reason)
{
    if (m_jogActiveDirection == 0) return;

    const int dir = m_jogActiveDirection;
    const auto oldTarget = m_activeJogTarget.value_or(currentAxisTarget());
    const auto newTarget = currentAxisTarget();
    qDebug() << "[MotionCtrl] retargetActiveJog reason=" << reason
             << "old=" << QString::fromStdString(application_vnext::control::axisTargetName(oldTarget))
             << "new=" << QString::fromStdString(application_vnext::control::axisTargetName(newTarget))
             << "dir=" << dir;
    LOG_INFO(LogLayer::UI, "MotionCtrl",
             "retargetActiveJog reason=" + std::string(reason ? reason : "")
             + " old=" + application_vnext::control::axisTargetName(oldTarget)
             + " new=" + application_vnext::control::axisTargetName(newTarget)
             + " dir=" + std::to_string(dir));

    stopActiveJog(reason);
    m_jogReleaseTimer.invalidate();
    startJogForTarget(newTarget, dir, reason);
}

application_vnext::control::AxisTarget MotionController::currentAxisTarget() const
{
    application_vnext::control::AxisTarget t;
    if (m_axisModel) {
        t.group = m_axisModel->currentGroup();
    }
    switch (m_currentAxis) {
        case AxisId::Y:  t.function = domain_vnext::model::AxisFunction::Y; break;
        case AxisId::Z:  t.function = domain_vnext::model::AxisFunction::Z; break;
        case AxisId::R:  t.function = domain_vnext::model::AxisFunction::R; break;
        case AxisId::X:  t.function = domain_vnext::model::AxisFunction::X; break;
        case AxisId::X1: t.function = domain_vnext::model::AxisFunction::X1; break;
        case AxisId::X2: t.function = domain_vnext::model::AxisFunction::X2; break;
    }
    return t;
}

void MotionController::submit(application_vnext::control::ControlCommand cmd)
{
    const std::string actionName =
        application_vnext::control::controlActionName(cmd.action);
    const std::string targetName =
        application_vnext::control::axisTargetName(cmd.target);

    if (!m_service) {
        qDebug() << "[MotionCtrl] submit skipped: service not connected action="
                 << QString::fromStdString(actionName)
                 << "axis=" << QString::fromStdString(targetName);
        return;
    }

    cmd.operationId = m_service->submit(std::move(cmd));
    qDebug() << "[MotionCtrl] submitted action=" << QString::fromStdString(actionName)
             << "axis=" << QString::fromStdString(targetName)
             << "op=" << QString::fromStdString(cmd.operationId);
}
