#pragma once

#include <QObject>
#include <QString>

class JoystickViewModel;
class QtAxisViewModel;

/**
 * @brief 摇杆 ViewModel 的 Qt 适配层
 *
 * 将 JoystickViewModel 的属性暴露为 Q_PROPERTY，供 QML 数据绑定使用。
 *
 * 设计原则：
 *   - 不持有 JoystickViewModel 的所有权（外部管理生命周期）
 *   - 将 JoystickDirection 映射为 int / QString 供 QML 消费
 */
class QtJoystickViewModel : public QObject {
    Q_OBJECT

    // ── 连接状态 ──
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY deviceCountChanged)

    // ── UI 模式 ──
    Q_PROPERTY(int uiMode READ uiMode WRITE setUIMode NOTIFY uiModeChanged)

    // ── 步进距离 ──
    Q_PROPERTY(double stepDistance READ stepDistance WRITE setStepDistance
               NOTIFY stepDistanceChanged)

    // ── 死区 ──
    Q_PROPERTY(float deadzone READ deadzone WRITE setDeadzone NOTIFY deadzoneChanged)

    // ── 启用/禁用（弹窗控制）──
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)

    // ── 当前方向（供调试/状态显示）──
    Q_PROPERTY(int currentDirection READ currentDirection NOTIFY directionChanged)
    Q_PROPERTY(QString directionText READ directionText NOTIFY directionChanged)

public:
    explicit QtJoystickViewModel(JoystickViewModel* core, QObject* parent = nullptr);

    // ── Getters ──
    bool connected() const;
    int deviceCount() const;
    int uiMode() const;
    double stepDistance() const;
    float deadzone() const;
    bool isEnabled() const;
    int currentDirection() const;
    QString directionText() const;

    // ── Setters ──
    void setUIMode(int mode);
    void setStepDistance(double mm);
    void setDeadzone(float dz);
    void setEnabled(bool enabled);

    // ── UI 信息注入（由 QML 层主动调用）──
    Q_INVOKABLE void bindToAxis(QtAxisViewModel* axisVM);
    Q_INVOKABLE void unbindAxis();

    // ── 帧驱动 ──
    void tick();

signals:
    void connectedChanged();
    void deviceCountChanged();
    void uiModeChanged();
    void stepDistanceChanged();
    void deadzoneChanged();
    void enabledChanged();
    void directionChanged();

private:
    JoystickViewModel* m_core;

    // ── 缓存节流（避免每帧 emit）──
    bool m_lastConnected = false;
    int  m_lastDeviceCount = 0;
    int  m_lastUIMode = 0;
    double m_lastStepDistance = 1.0;
    float m_lastDeadzone = 0.15f;
    bool m_lastEnabled = true;
    int  m_lastDirection = 0;
};