#ifndef QT_AXIS_VIEW_MODEL_H
#define QT_AXIS_VIEW_MODEL_H

#include <QObject>
#include <QVariantList>
#include "AxisViewModelCore.h"

class QtAxisViewModel : public QObject {
    Q_OBJECT

    // 现有属性（保留）
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(double absPos READ absPos NOTIFY absPosChanged)
    Q_PROPERTY(double relPos READ relPos NOTIFY relPosChanged)

    Q_PROPERTY(double posLimit READ posLimit NOTIFY limitsChanged)
    Q_PROPERTY(double negLimit READ negLimit NOTIFY limitsChanged)
    Q_PROPERTY(double jogVelocity READ jogVelocity WRITE setJogVelocity NOTIFY velocityChanged)
    Q_PROPERTY(double moveVelocity READ moveVelocity WRITE setMoveVelocity NOTIFY velocityChanged)

    // 错误接口
    Q_PROPERTY(bool hasError READ hasError NOTIFY errorChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY errorChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)

    // ⭐ 新增属性（修复 #1, #5, #6）
    Q_PROPERTY(bool isEnabled READ isEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(QString errorCategory READ errorCategory NOTIFY errorChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY errorCountChanged)

public:
    explicit QtAxisViewModel(AxisViewModelCore* core, QObject *parent = nullptr);

    // Getters
    int state() const;
    double absPos() const;
    double relPos() const;
    double posLimit() const;
    double negLimit() const;
    double jogVelocity() const;
    double moveVelocity() const;
    bool hasError() const;
    QString errorCode() const;
    QString errorMessage() const;

    // ⭐ 新增 Getters
    bool isEnabled() const;
    QString stateText() const;
    QString errorCategory() const;
    int errorCount() const;

    // ===== P0: 使能/去使能接口（新增） =====
    Q_INVOKABLE void enable();
    Q_INVOKABLE void disable();

    // 控制输入 (严格对齐已实现的 Core 方法)
    Q_INVOKABLE void jogPositivePressed();
    Q_INVOKABLE void jogPositiveReleased();
    Q_INVOKABLE void jogNegativePressed();
    Q_INVOKABLE void jogNegativeReleased();

    Q_INVOKABLE void moveAbsolute(double targetPos);
    Q_INVOKABLE void moveRelative(double distance);

    Q_INVOKABLE void setJogVelocity(double v);
    Q_INVOKABLE void setMoveVelocity(double v);

    Q_INVOKABLE void stop();

    // 零位操作
    Q_INVOKABLE void zeroAbsolutePosition();
    Q_INVOKABLE void setRelativeZero();
    Q_INVOKABLE void clearRelativeZero();

    // 错误管理
    Q_INVOKABLE void clearError();

    // ⭐ 新增 Q_INVOKABLE（支持错误确认）
    Q_INVOKABLE QVariantList getAllErrors() const;
    Q_INVOKABLE void acknowledgeError(int index);
    Q_INVOKABLE void acknowledgeAllErrors();

    // 系统推进
    void tick();

signals:
    void stateChanged();
    void absPosChanged();
    void relPosChanged();
    void limitsChanged();
    void velocityChanged();
    void errorChanged();

    // ⭐ 新增 signal
    void errorCountChanged();

private:
    AxisViewModelCore* m_core;

    // 缓存节流
    AxisState  m_lastState;
    double    m_lastAbsPos;
    double    m_lastRelPos;
    bool      m_lastHasError   = false;
    QString   m_lastErrorCode;
    QString   m_lastErrorMessage;

    // ⭐ 新增缓存成员（修复 #7）
    double    m_lastJogVelocity  = 0.0;
    double    m_lastMoveVelocity = 0.0;
    int       m_lastErrorCount   = 0;

    const double EPSILON = 0.001;
};

#endif // QT_AXIS_VIEW_MODEL_H
