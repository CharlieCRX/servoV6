#ifndef QT_AXIS_VIEW_MODEL_H
#define QT_AXIS_VIEW_MODEL_H

#include <QObject>
#include "AxisViewModelCore.h"

class QtAxisViewModel : public QObject {
    Q_OBJECT

    // 仅保留当前 Core 已实现的状态
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(double absPos READ absPos NOTIFY absPosChanged)

public:
    explicit QtAxisViewModel(AxisViewModelCore* core, QObject *parent = nullptr);

    // Getters
    int state() const;
    double absPos() const;

    // 控制输入 (严格对齐已实现的 Core 方法)
    Q_INVOKABLE void jogPositivePressed();
    Q_INVOKABLE void jogPositiveReleased();
    Q_INVOKABLE void jogNegativePressed();
    Q_INVOKABLE void jogNegativeReleased();
    
    Q_INVOKABLE void moveAbsolute(double targetPos);
    Q_INVOKABLE void stop();

    // 系统推进
    void tick();

signals:
    void stateChanged();
    void absPosChanged();

private:
    AxisViewModelCore* m_core;

    // 缓存节流
    AxisState m_lastState;
    double m_lastAbsPos;

    const double EPSILON = 0.001;
};

#endif // QT_AXIS_VIEW_MODEL_H