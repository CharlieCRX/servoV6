// ============================================================================
// UiControlAdapter.h —— Phase 2：统一状态快照 -> Qt/QML 只读适配器
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 2 / §8.1。
//
// 目标：UI 开始展示统一快照（只读），仍由旧 Core 发控制，仅用于对照验证
// 投影正确性。本适配器在 GUI 线程（QTimer / QueuedConnection 内）定时调用
// refresh()：读取 MotionControlService::store().snapshot() + globallyLocked()，
// 经 UiProjection（纯 C++）投影为 Q_PROPERTY / Q_INVOKABLE。
//
// 约束：
//   - 不在 UDP/PLC 线程操作 Qt 控件；Store 无订阅者、不持锁回调（snapshot()
//     内部锁内仅拷贝，投影在锁外 GUI 线程完成）；
//   - **Phase 2 严格只读**：不暴露 submit()，不提供任何可写入口；
//   - service 可传 nullptr（Phase 6 注入真实 MotionControlService 前，展示默认
//     离线/锁定态，QML 绑定安全）。
//
// 依赖 Qt（QObject/QVariant）+ application_vnext（投影与快照类型）。
// ============================================================================
#ifndef UI_CONTROL_ADAPTER_H
#define UI_CONTROL_ADAPTER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace application_vnext::control {
class MotionControlService;
struct ProjectedUiState;
}  // namespace application_vnext::control

class UiControlAdapter : public QObject {
    Q_OBJECT

    // ---- 连接 / 安全 / 全局锁定（快照投影）----
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(QString connectionDiagnostic READ connectionDiagnostic NOTIFY stateChanged)
    Q_PROPERTY(bool emergencyStop READ emergencyStop NOTIFY stateChanged)
    Q_PROPERTY(bool safetyTrusted READ safetyTrusted NOTIFY stateChanged)
    Q_PROPERTY(bool globallyLocked READ globallyLocked NOTIFY stateChanged)

    // ---- 轴 / 龙门 / 操作条目（只读，QML 可遍历）----
    Q_PROPERTY(QVariantList axes READ axes NOTIFY stateChanged)
    Q_PROPERTY(QVariantList gantries READ gantries NOTIFY stateChanged)
    Q_PROPERTY(QVariantList operations READ operations NOTIFY stateChanged)

public:
    /// svc 可为空：Phase 2（真实 vnext 链路接入前）展示默认离线/锁定态；
    /// Phase 6 由组合根注入真实 MotionControlService。
    explicit UiControlAdapter(application_vnext::control::MotionControlService* svc,
                              QObject* parent = nullptr);
    ~UiControlAdapter() override;

    // ---- getters（读最近一次投影缓存）----
    bool connected() const;
    QString connectionDiagnostic() const;
    bool emergencyStop() const;
    bool safetyTrusted() const;
    bool globallyLocked() const;
    QVariantList axes() const;
    QVariantList gantries() const;
    QVariantList operations() const;

    /// 在 GUI 线程定时调用：读 Store 快照并重新投影、emit stateChanged()。
    Q_INVOKABLE void refresh();

    // ---- 便捷查询（QML 绑定用）----
    /// 按 PLC 槽位下标查询轴快照（QVariantMap，无匹配返回空 map）。
    Q_INVOKABLE QVariantMap axis(int slot) const;
    /// 按 组字母("A"/"B") + 角色("Y"/"X"/"X1"/...) 查询轴快照。
    Q_INVOKABLE QVariantMap axisFor(const QString& groupLetter, const QString& role) const;
    /// 按组索引(0/1)查询龙门快照。
    Q_INVOKABLE QVariantMap gantry(int group) const;
    /// 按 operationId 查询操作条目。
    Q_INVOKABLE QVariantMap operation(const QString& operationId) const;

signals:
    /// 快照投影更新（QML 绑定据此自动刷新）。
    void stateChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

#endif // UI_CONTROL_ADAPTER_H
