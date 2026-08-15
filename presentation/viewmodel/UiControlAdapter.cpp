// ============================================================================
// UiControlAdapter.cpp —— Phase 2：统一状态快照 -> Qt/QML 只读适配器实现
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 2 / §8.1。
//
// refresh() 在 GUI 线程调用：读 store().snapshot()（线程安全，锁内仅拷贝）与
// globallyLocked()，经 UiProjection（纯 C++、无 Qt）投影为 QVariant，供 QML 绑定。
// 不在 UDP/PLC 线程操作 Qt 控件；Store 无订阅者回调。
// ============================================================================
#include "presentation/viewmodel/UiControlAdapter.h"

#include <QVariant>

#include "application_vnext/control/MotionControlService.h"
#include "application_vnext/control/UiProjection.h"

namespace {

using application_vnext::control::AxisUiView;
using application_vnext::control::GantryUiView;
using application_vnext::control::OperationView;
using application_vnext::control::ProjectedUiState;

QVariantMap axisToMap(const AxisUiView& a) {
    QVariantMap m;
    m["slot"] = static_cast<int>(a.slot);
    m["group"] = QString::fromStdString(a.groupLetter);
    m["role"] = QString::fromStdString(a.roleName);
    m["displayName"] = QString::fromStdString(a.displayName);
    m["hmiVisible"] = a.hmiVisible;
    m["bound"] = a.bound;
    m["trusted"] = a.trusted;
    m["locked"] = a.locked;
    m["absPosition"] = static_cast<double>(a.absPosition);
    m["relPosition"] = static_cast<double>(a.relPosition);
    m["manualSpeed"] = static_cast<double>(a.manualSpeed);
    m["positioningSpeed"] = static_cast<double>(a.positioningSpeed);
    m["absMoveTarget"] = static_cast<double>(a.absMoveTarget);
    m["relMoveTarget"] = static_cast<double>(a.relMoveTarget);
    m["motionState"] = static_cast<int>(a.motionState);
    m["motionStateName"] = QString::fromStdString(a.motionStateName);
    m["motionLimit"] = static_cast<int>(a.motionLimit);
    m["alarmWord"] = static_cast<int>(a.alarmWord);
    m["leased"] = a.leased;
    m["leaseOwnerName"] = QString::fromStdString(a.leaseOwnerName);
    return m;
}

QVariantMap gantryToMap(const GantryUiView& g) {
    QVariantMap m;
    m["group"] = QString::fromStdString(g.groupLetter);
    m["trusted"] = g.trusted;
    m["state"] = static_cast<int>(g.state);
    m["stateName"] = QString::fromStdString(g.stateName);
    m["internalStep"] = static_cast<int>(g.internalStep);
    m["commandResult"] = static_cast<int>(g.commandResult);
    m["commandErrorCode"] = static_cast<int>(g.commandErrorCode);
    m["logicalControlAllowed"] = g.logicalControlAllowed;
    m["memberControlAllowed"] = g.memberControlAllowed;
    m["readyToCouple"] = g.readyToCouple;
    m["readyToDecouple"] = g.readyToDecouple;
    m["x1InGear"] = g.x1InGear;
    m["x2InGear"] = g.x2InGear;
    m["logicalPosition"] = static_cast<double>(g.logicalPosition);
    m["skew"] = static_cast<double>(g.skew);
    m["fault"] = g.fault;
    m["faultCode"] = static_cast<int>(g.faultCode);
    m["lifecycleLeased"] = g.lifecycleLeased;
    return m;
}

QVariantMap operationToMap(const OperationView& op) {
    QVariantMap m;
    m["operationId"] = QString::fromStdString(op.operationId);
    m["source"] = QString::fromStdString(op.sourceName);
    m["axis"] = QString::fromStdString(op.axis);
    m["kind"] = QString::fromStdString(op.kindName);
    m["state"] = QString::fromStdString(op.stateName);
    m["diag"] = QString::fromStdString(op.diag);
    m["motionState"] = static_cast<int>(op.motionState);
    m["position"] = static_cast<double>(op.position);
    return m;
}

}  // namespace

struct UiControlAdapter::Impl {
    application_vnext::control::MotionControlService* svc = nullptr;
    ProjectedUiState projected;
};

UiControlAdapter::UiControlAdapter(application_vnext::control::MotionControlService* svc,
                                   QObject* parent)
    : QObject(parent)
    , d_(new Impl) {
    d_->svc = svc;
    refresh();  // 初始投影（svc 为空时展示默认离线/锁定态）
}

UiControlAdapter::~UiControlAdapter() = default;

bool UiControlAdapter::connected() const { return d_->projected.connected; }
QString UiControlAdapter::connectionDiagnostic() const {
    return QString::fromStdString(d_->projected.connectionDiagnostic);
}
bool UiControlAdapter::emergencyStop() const { return d_->projected.emergencyStop; }
bool UiControlAdapter::safetyTrusted() const { return d_->projected.safetyTrusted; }
bool UiControlAdapter::globallyLocked() const { return d_->projected.globallyLocked; }

QVariantList UiControlAdapter::axes() const {
    QVariantList out;
    out.reserve(d_->projected.axes.size());
    for (const auto& a : d_->projected.axes) out.append(axisToMap(a));
    return out;
}

QVariantList UiControlAdapter::gantries() const {
    QVariantList out;
    out.reserve(d_->projected.gantries.size());
    for (const auto& g : d_->projected.gantries) out.append(gantryToMap(g));
    return out;
}

QVariantList UiControlAdapter::operations() const {
    QVariantList out;
    out.reserve(d_->projected.operations.size());
    for (const auto& op : d_->projected.operations) out.append(operationToMap(op));
    return out;
}

void UiControlAdapter::refresh() {
    if (d_->svc) {
        // snapshot() 线程安全（锁内仅拷贝）；globallyLocked() 为协调层当前锁定标志。
        d_->projected = application_vnext::control::UiProjection::project(
            d_->svc->store().snapshot(), d_->svc->globallyLocked());
    } else {
        // Phase 2：真实 vnext 链路接入前，展示默认离线/全局锁定态（安全默认）。
        d_->projected = application_vnext::control::UiProjection::project(
            application_vnext::control::ControlStateSnapshot{}, /*globallyLocked=*/true);
    }
    emit stateChanged();
}

QVariantMap UiControlAdapter::axis(int slot) const {
    for (const auto& a : d_->projected.axes) {
        if (a.slot == static_cast<int16_t>(slot)) return axisToMap(a);
    }
    return {};
}

QVariantMap UiControlAdapter::axisFor(const QString& groupLetter, const QString& role) const {
    const std::string g = groupLetter.toStdString();
    const std::string r = role.toStdString();
    for (const auto& a : d_->projected.axes) {
        if (a.groupLetter == g && a.roleName == r) return axisToMap(a);
    }
    return {};
}

QVariantMap UiControlAdapter::gantry(int group) const {
    if (group < 0 || group >= static_cast<int>(d_->projected.gantries.size())) return {};
    return gantryToMap(d_->projected.gantries[static_cast<std::size_t>(group)]);
}

QVariantMap UiControlAdapter::operation(const QString& operationId) const {
    const std::string id = operationId.toStdString();
    for (const auto& op : d_->projected.operations) {
        if (op.operationId == id) return operationToMap(op);
    }
    return {};
}
