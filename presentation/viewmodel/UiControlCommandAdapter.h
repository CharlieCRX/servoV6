// ============================================================================
// UiControlCommandAdapter.h —— UI 唯一可写入口（ControlCommand source=Ui）
// ============================================================================
// 依据《UI 新链路控制验证实施清单》§3.1 / 《MotionControlService》§8.1。
//
// UI 所有写操作只经本适配器提交 `ControlCommand{source=Ui}` 到 MotionControlService，
// 不持有 PLC 地址 / SystemManagerVnext / AxisMotionApi / GantryMotionApi。返回
// `operationId`（"" 表示本地校验失败 / 未注入 service）；操作最终结果以 operationId
// 查询 / 快照回显为准，点击成功不等于 PLC 已执行。
//
// 约束：
//   - 只做轻量输入校验（空目标 / 非正速度 / 未注入 service），最终权威校验在
//     MotionControlService（租约 / 会话 / 全局锁定 / 零速度拒绝）；
//   - 急停 / 解除急停 / 停止不受普通按钮锁定规则影响，始终可提交。
//
// 依赖 Qt（QObject/QString）+ application_vnext（ControlCommand/MotionControlService）。
// ============================================================================
#ifndef UI_CONTROL_COMMAND_ADAPTER_H
#define UI_CONTROL_COMMAND_ADAPTER_H

#include <QObject>
#include <QString>

namespace application_vnext::control {
class MotionControlService;
struct AxisTarget;
enum class ControlAction;
}  // namespace application_vnext::control

namespace domain_vnext::model {
enum class AxisFunction;
}  // namespace domain_vnext::model

class UiControlCommandAdapter : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    /// svc 可传 nullptr：真实 vnext 链路接入前，所有提交返回 ""（安全默认）。
    explicit UiControlCommandAdapter(application_vnext::control::MotionControlService* svc,
                                     QObject* parent = nullptr);
    ~UiControlCommandAdapter() override;

    // ---- 参数 / 预置（一次性写入；仅预置不启动运动）----
    Q_INVOKABLE QString setManualSpeed(const QString& group, const QString& role, double value);
    Q_INVOKABLE QString setPositioningSpeed(const QString& group, const QString& role,
                                            double value);
    Q_INVOKABLE QString setAbsTarget(const QString& group, const QString& role, double value);
    Q_INVOKABLE QString setRelTarget(const QString& group, const QString& role, double value);
    Q_INVOKABLE QString setRelZero(const QString& group, const QString& role);

    // ---- 使能 ----
    Q_INVOKABLE QString enableAxis(const QString& group, const QString& role, bool on);
    Q_INVOKABLE QString enableMotor(const QString& group, const QString& role, bool on);

    // ---- 点动（按住/松开成对提交）----
    Q_INVOKABLE QString startJogForward(const QString& group, const QString& role);
    Q_INVOKABLE QString startJogBackward(const QString& group, const QString& role);
    Q_INVOKABLE QString stopJog(const QString& group, const QString& role);

    // ---- 定位（target + speed 原子携带，speed 必须 >0）----
    Q_INVOKABLE QString startAbsMove(const QString& group, const QString& role,
                                     double target, double speed);
    Q_INVOKABLE QString startRelMove(const QString& group, const QString& role,
                                     double delta, double speed);

    // ---- 停止 / 急停（不受普通锁定规则影响）----
    Q_INVOKABLE QString stopMotion(const QString& group, const QString& role);
    Q_INVOKABLE QString triggerEmergencyStop();
    Q_INVOKABLE QString requestEmergencyStopRelease();

    /// 最近一次提交失败的本地诊断（成功/未提交返回空）。
    Q_INVOKABLE QString lastError() const;
    bool available() const;

signals:
    void lastErrorChanged();

private:
    void setLastError(const QString& error);
    QString submitUi(application_vnext::control::AxisTarget target,
                     application_vnext::control::ControlAction action,
                     double value = 0.0, bool level = false,
                     bool hasMotion = false, double motionTarget = 0.0,
                     double motionSpeed = 0.0);
    bool parseAxis(const QString& group, const QString& role,
                   application_vnext::control::AxisTarget& out);

    struct Impl;
    Impl* d_;
};

#endif // UI_CONTROL_COMMAND_ADAPTER_H
