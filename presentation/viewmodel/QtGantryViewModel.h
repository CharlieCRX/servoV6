#ifndef QT_GANTRY_VIEW_MODEL_H
#define QT_GANTRY_VIEW_MODEL_H

#include <QObject>
#include <QString>
#include <QVector>
#include "presentation/viewmodel/GantryViewModelCore.h"

/**
 * @file QtGantryViewModel.h
 * @brief Qt 包装器 — 将 GantryViewModelCore 的状态投影暴露为 Q_PROPERTY
 *
 * 职责：
 *   - 将 GantryViewModelCore 的 C++ 原生类型映射为 QML 兼容的 Q_PROPERTY
 *   - 提供 Q_INVOKABLE 方法供 QML 调用
 *   - 缓存节流：避免高频信号 flooding
 */
class QtGantryViewModel : public QObject {
    Q_OBJECT

    enum class GantryUiState {
        Standby,        // 下电待机
        Active,         // 上电并联动
        Maintenance     // 上电但解耦 (维护模式)
    };
    Q_ENUM(GantryUiState)

    // ── 龙门模式 ──
    Q_PROPERTY(bool coupled READ isCoupled NOTIFY coupledChanged)
    Q_PROPERTY(GantryUiState uiState READ uiState NOTIFY uiStateChanged)

    // ── 状态 ──
    Q_PROPERTY(int aggregatedState READ aggregatedState NOTIFY aggregatedStateChanged)

    // ── 位置 ──
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double x1Position READ x1Position NOTIFY x1PositionChanged)
    Q_PROPERTY(double x2Position READ x2Position NOTIFY x2PositionChanged)

    // ── 使能 ──
    Q_PROPERTY(bool x1Enabled READ x1Enabled NOTIFY x1EnabledChanged)
    Q_PROPERTY(bool x2Enabled READ x2Enabled NOTIFY x2EnabledChanged)

    // ── 报警/限位 ──
    Q_PROPERTY(bool isAnyAlarm READ isAnyAlarm NOTIFY alarmChanged)
    Q_PROPERTY(bool isAnyLimit READ isAnyLimit NOTIFY limitChanged)
    Q_PROPERTY(bool x1PosLimit READ x1PosLimit NOTIFY x1LimitChanged)
    Q_PROPERTY(bool x1NegLimit READ x1NegLimit NOTIFY x1LimitChanged)
    Q_PROPERTY(bool x2PosLimit READ x2PosLimit NOTIFY x2LimitChanged)
    Q_PROPERTY(bool x2NegLimit READ x2NegLimit NOTIFY x2LimitChanged)

    // ── 命令槽 ──
    Q_PROPERTY(bool canAcceptCommand READ canAcceptCommand NOTIFY canAcceptCommandChanged)
    Q_PROPERTY(bool canCouple READ canCouple NOTIFY canCoupleChanged)

    // ── Jog 速度 (UI 面) ──
    Q_PROPERTY(double jogVelocity READ jogVelocity WRITE setJogVelocity NOTIFY jogVelocityChanged)

    // ── 描述 ──
    Q_PROPERTY(QString stateDescription READ stateDescription NOTIFY stateDescriptionChanged)
    Q_PROPERTY(QString lastCommandResult READ lastCommandResult NOTIFY lastCommandResultChanged)

public:
    explicit QtGantryViewModel(GantryViewModelCore* core, QObject* parent = nullptr);

    // ── Getters ──
    bool isCoupled() const;
    int aggregatedState() const;
    double position() const;
    double x1Position() const;
    double x2Position() const;
    bool x1Enabled() const;
    bool x2Enabled() const;
    bool isAnyAlarm() const;
    bool isAnyLimit() const;
    bool x1PosLimit() const;
    bool x1NegLimit() const;
    bool x2PosLimit() const;
    bool x2NegLimit() const;
    bool canAcceptCommand() const;
    bool canCouple() const;
    QString stateDescription() const;
    QString lastCommandResult() const;
    double jogVelocity() const;

    // ── 控制指令 (Q_INVOKABLE) ──
    // 联动控制指令
    Q_INVOKABLE void toggleUnifiedPower(); // 主按钮触发：在 Standby 和 Active 间切换
    Q_INVOKABLE void requestMaintenanceMode(const QString& password); // 申请进入维护模式
    Q_INVOKABLE void exitMaintenanceMode(); // 退出维护模式回到 Active

    /// 耦合/解耦
    Q_INVOKABLE void requestCoupling();
    Q_INVOKABLE void requestDecoupling(const QString& reason = QString());

    /// Jog 分动 (X1/X2)
    Q_INVOKABLE void jogX1ForwardPressed();
    Q_INVOKABLE void jogX1ReversePressed();
    Q_INVOKABLE void jogX2ForwardPressed();
    Q_INVOKABLE void jogX2ReversePressed();

    /// Jog 联动 (逻辑轴 X)
    Q_INVOKABLE void jogCoupledForwardPressed();
    Q_INVOKABLE void jogCoupledReversePressed();
    Q_INVOKABLE void jogCoupledReleased();

    /// Move
    Q_INVOKABLE void moveAbsoluteX1(double target);
    Q_INVOKABLE void moveAbsoluteX2(double target);
    Q_INVOKABLE void moveAbsoluteCoupled(double target);
    Q_INVOKABLE void moveRelativeX1(double delta);
    Q_INVOKABLE void moveRelativeX2(double delta);
    Q_INVOKABLE void moveRelativeCoupled(double delta);

    /// Move (QML 文本输入便捷方法)
    Q_INVOKABLE void moveCoupledTo(const QString& text);
    Q_INVOKABLE void moveX1To(const QString& text);
    Q_INVOKABLE void moveX2To(const QString& text);
    Q_INVOKABLE void moveCoupledRel(const QString& text);
    Q_INVOKABLE void moveX1Rel(const QString& text);
    Q_INVOKABLE void moveX2Rel(const QString& text);

    /// Jog 速度控制 (UI 面)
    Q_INVOKABLE void setJogVelocity(double v);
    Q_INVOKABLE void adjustJogVelocity(double delta);

    /// 获取指令日志
    Q_INVOKABLE QString getCommandLog() const;

    /// 急停
    Q_INVOKABLE void stop();

    /// 每周期 tick
    void tick();

signals:
    void coupledChanged();
    void aggregatedStateChanged();
    void positionChanged();
    void x1PositionChanged();
    void x2PositionChanged();
    void x1EnabledChanged();
    void x2EnabledChanged();
    void alarmChanged();
    void limitChanged();
    void x1LimitChanged();
    void x2LimitChanged();
    void canAcceptCommandChanged();
    void canCoupleChanged();
    void jogVelocityChanged();
    void stateDescriptionChanged();
    void lastCommandResultChanged();

private:
    GantryViewModelCore* m_core;

    // 缓存节流
    bool m_lastCoupled;
    int m_lastAggState;
    double m_lastPos;
    double m_lastX1Pos;
    double m_lastX2Pos;
    bool m_lastX1Enabled;
    bool m_lastX2Enabled;
    bool m_lastAnyAlarm;
    bool m_lastAnyLimit;
    bool m_lastX1PosLimit;
    bool m_lastX1NegLimit;
    bool m_lastX2PosLimit;
    bool m_lastX2NegLimit;
    bool m_lastCanAccept;
    bool m_lastCanCouple;
    double m_jogVelocity = 15.0;   // UI 面 Jog 速度 mm/s
    QString m_lastDesc;
    QString m_lastCmdResult;

    static constexpr double EPSILON = 0.001;
};

#endif // QT_GANTRY_VIEW_MODEL_H
