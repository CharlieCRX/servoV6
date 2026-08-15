// ============================================================================
// UiControlCommandAdapter.cpp —— UI 唯一可写入口实现
// ============================================================================
// 见头文件注释。把 (group, role) 映射为 AxisTarget{PlcGroupIndex, AxisFunction}，
// 构造 ControlCommand{source=Ui} 并 submit；返回 operationId，失败返回 ""。
// ============================================================================
#include "presentation/viewmodel/UiControlCommandAdapter.h"

#include <utility>

#include "application_vnext/control/ControlCommand.h"
#include "application_vnext/control/MotionControlService.h"
#include "domain_vnext/model/AxisFunction.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

struct UiControlCommandAdapter::Impl {
    application_vnext::control::MotionControlService* svc = nullptr;
    QString lastError;
};

UiControlCommandAdapter::UiControlCommandAdapter(
    application_vnext::control::MotionControlService* svc, QObject* parent)
    : QObject(parent), d_(new Impl) {
    d_->svc = svc;
}

UiControlCommandAdapter::~UiControlCommandAdapter() { delete d_; }

QString UiControlCommandAdapter::lastError() const { return d_->lastError; }

bool UiControlCommandAdapter::parseAxis(const QString& group, const QString& role,
                                        application_vnext::control::AxisTarget& out) {
    using domain_vnext::model::AxisFunction;
    if (group == "A") out.group = plc_vnext::contracts::PlcGroupIndex(0);
    else if (group == "B") out.group = plc_vnext::contracts::PlcGroupIndex(1);
    else { d_->lastError = "未知组: " + group; return false; }
    const QString r = role.toUpper();
    if (r == "X")  out.function = AxisFunction::X;
    else if (r == "X1") out.function = AxisFunction::X1;
    else if (r == "X2") out.function = AxisFunction::X2;
    else if (r == "Y")  out.function = AxisFunction::Y;
    else if (r == "Z")  out.function = AxisFunction::Z;
    else if (r == "R")  out.function = AxisFunction::R;
    else { d_->lastError = "未知角色: " + role; return false; }
    return true;
}

QString UiControlCommandAdapter::submitUi(application_vnext::control::AxisTarget target,
                                          application_vnext::control::ControlAction action,
                                          double value, bool level,
                                          bool hasMotion, double motionTarget,
                                          double motionSpeed) {
    d_->lastError.clear();
    if (!d_->svc) { d_->lastError = "控制服务未注入"; return QString(); }

    application_vnext::control::ControlCommand cmd;
    cmd.source = application_vnext::control::ControlSource::Ui;
    cmd.target = target;
    cmd.action = action;
    cmd.value = static_cast<float>(value);
    cmd.level = level;
    if (hasMotion) {
        cmd.motion = application_vnext::control::MotionRequest{
            static_cast<float>(motionTarget), static_cast<float>(motionSpeed)};
    }
    return QString::fromStdString(d_->svc->submit(std::move(cmd)));
}

QString UiControlCommandAdapter::setManualSpeed(const QString& g, const QString& r, double v) {
    application_vnext::control::AxisTarget t;
    if (!parseAxis(g, r, t)) return QString();
    return submitUi(t, application_vnext::control::ControlAction::SetManualSpeed, v);
}
QString UiControlCommandAdapter::setPositioningSpeed(const QString& g, const QString& r,
                                                     double v) {
    application_vnext::control::AxisTarget t;
    if (!parseAxis(g, r, t)) return QString();
    return submitUi(t, application_vnext::control::ControlAction::SetPositioningSpeed, v);
}
QString UiControlCommandAdapter::setAbsTarget(const QString& g, const QString& r, double v) {
    application_vnext::control::AxisTarget t;
    if (!parseAxis(g, r, t)) return QString();
    return submitUi(t, application_vnext::control::ControlAction::SetAbsTarget, v);
}
QString UiControlCommandAdapter::setRelTarget(const QString& g, const QString& r, double v) {
    application_vnext::control::AxisTarget t;
    if (!parseAxis(g, r, t)) return QString();
    return submitUi(t, application_vnext::control::ControlAction::SetRelTarget, v);
}
QString UiControlCommandAdapter::setRelZero(const QString& g, const QString& r) {
    application_vnext::control::AxisTarget t;
    if (!parseAxis(g, r, t)) return QString();
    return submitUi(t, application_vnext::control::ControlAction::SetRelZero);
}
QString UiControlCommandAdapter::enableAxis(const QString& g, const QString& r, bool on) {
    application_vnext::control::AxisTarget t;
    if (!parseAxis(g, r, t)) return QString();
    return submitUi(t, application_vnext::control::ControlAction::EnableAxis, 0.0, on);
}
QString UiControlCommandAdapter::enableMotor(const QString& g, const QString& r, bool on) {
    application_vnext::control::AxisTarget t;
    if (!parseAxis(g, r, t)) return QString();
    return submitUi(t, application_vnext::control::ControlAction::EnableMotor, 0.0, on);
}
QString UiControlCommandAdapter::startJogForward(const QString& g, const QString& r) {
    application_vnext::control::AxisTarget t;
    if (!parseAxis(g, r, t)) return QString();
    return submitUi(t, application_vnext::control::ControlAction::StartJogForward);
}
QString UiControlCommandAdapter::startJogBackward(const QString& g, const QString& r) {
    application_vnext::control::AxisTarget t;
    if (!parseAxis(g, r, t)) return QString();
    return submitUi(t, application_vnext::control::ControlAction::StartJogBackward);
}
QString UiControlCommandAdapter::stopJog(const QString& g, const QString& r) {
    application_vnext::control::AxisTarget t;
    if (!parseAxis(g, r, t)) return QString();
    return submitUi(t, application_vnext::control::ControlAction::StopJog);
}
QString UiControlCommandAdapter::startAbsMove(const QString& g, const QString& r,
                                              double target, double speed) {
    if (speed <= 0.0) { d_->lastError = "定位速度必须为正"; return QString(); }
    application_vnext::control::AxisTarget t;
    if (!parseAxis(g, r, t)) return QString();
    return submitUi(t, application_vnext::control::ControlAction::StartAbsMove,
                    0.0, false, /*hasMotion=*/true, target, speed);
}
QString UiControlCommandAdapter::startRelMove(const QString& g, const QString& r,
                                              double delta, double speed) {
    if (speed <= 0.0) { d_->lastError = "定位速度必须为正"; return QString(); }
    application_vnext::control::AxisTarget t;
    if (!parseAxis(g, r, t)) return QString();
    return submitUi(t, application_vnext::control::ControlAction::StartRelMove,
                    0.0, false, /*hasMotion=*/true, delta, speed);
}
QString UiControlCommandAdapter::stopMotion(const QString& g, const QString& r) {
    application_vnext::control::AxisTarget t;
    if (!parseAxis(g, r, t)) return QString();
    return submitUi(t, application_vnext::control::ControlAction::StopMotion);
}
QString UiControlCommandAdapter::triggerEmergencyStop() {
    application_vnext::control::AxisTarget t;
    t.group = plc_vnext::contracts::PlcGroupIndex(0);
    t.function = domain_vnext::model::AxisFunction::Y;
    return submitUi(t, application_vnext::control::ControlAction::EmergencyStop);
}
QString UiControlCommandAdapter::requestEmergencyStopRelease() {
    application_vnext::control::AxisTarget t;
    t.group = plc_vnext::contracts::PlcGroupIndex(0);
    t.function = domain_vnext::model::AxisFunction::Y;
    return submitUi(t, application_vnext::control::ControlAction::ReleaseEmergencyStop);
}

