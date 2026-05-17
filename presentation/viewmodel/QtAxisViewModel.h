#ifndef QT_AXIS_VIEW_MODEL_H
#define QT_AXIS_VIEW_MODEL_H

#include <QObject>
#include "AxisViewModelCore.h"

class QtAxisViewModel : public QObject {
    Q_OBJECT

    // 仅保留当前 Core 已实现的状态
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(double absPos READ absPos NOTIFY absPosChanged)
    Q_PROPERTY(double relPos READ relPos NOTIFY relPosChanged)

    Q_PROPERTY(double posLimit READ posLimit NOTIFY limitsChanged)
    Q_PROPERTY(double negLimit READ negLimit NOTIFY limitsChanged)
    Q_PROPERTY(double jogVelocity READ jogVelocity WRITE setJogVelocity NOTIFY velocityChanged)
    Q_PROPERTY(double moveVelocity READ moveVelocity WRITE setMoveVelocity NOTIFY velocityChanged)

    // 错误接口（重构后新增）
    Q_PROPERTY(bool hasError READ hasError NOTIFY errorChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY errorChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)

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

    // 零位操作（重构后新增）
    Q_INVOKABLE void zeroAbsolutePosition();
    Q_INVOKABLE void setRelativeZero();
    Q_INVOKABLE void clearRelativeZero();

    Q_INVOKABLE void clearError();

    // 系统推进
    void tick();

signals:
    void stateChanged();
    void absPosChanged();
    void relPosChanged();
    void limitsChanged();
    void velocityChanged();
    void errorChanged();

private:
    AxisViewModelCore* m_core;

    // 缓存节流
    AxisState  m_lastState;
    double    m_lastAbsPos;
    double    m_lastRelPos;
    bool      m_lastHasError   = false;
    QString   m_lastErrorCode;
    QString   m_lastErrorMessage;

    const double EPSILON = 0.001;
};

#endif // QT_AXIS_VIEW_MODEL_H
