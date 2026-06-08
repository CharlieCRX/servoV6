#include "GamepadInputInterpreter.h"
#include "infrastructure/joystick/AndroidGamepadJoystick.h"
#include <QDebug>
#include <QElapsedTimer>

GamepadInputInterpreter::GamepadInputInterpreter(QObject* parent)
    : QObject(parent)
{
}

void GamepadInputInterpreter::start()
{
    auto& pad = AndroidGamepadJoystick::instance();
    connect(&pad, &AndroidGamepadJoystick::changed,
            this, &GamepadInputInterpreter::onGamepadChanged);
    qDebug() << "GamepadInputInterpreter started, listening to AndroidGamepadJoystick::changed()";
}

void GamepadInputInterpreter::onGamepadChanged()
{
    auto& pad = AndroidGamepadJoystick::instance();
    float lx = pad.lx();

    // ──── 左摇杆：选轴事件（死区 + 去抖）────
    // Step3 只做左摇杆验证，LX > 0.3 → AxisSelect Right，LX < -0.3 → AxisSelect Left
    if (lx > kDeadzone) {
        // LX > +0.3: 右拨
        qint64 now = QElapsedTimer::clockMsecs();
        if (now - m_lastAxisSelectTime > kAxisSelectDebounceMs) {
            m_lastAxisSelectTime = now;
            qDebug() << "AxisSelect Right  (LX =" << lx << ")";
            InputEvent event;
            event.type = InputEvent::Type::AxisSelect;
            event.axisDir = AxisSelectDirection::Right;
            emit inputEvent(event);
        }
    } else if (lx < -kDeadzone) {
        // LX < -0.3: 左拨
        qint64 now = QElapsedTimer::clockMsecs();
        if (now - m_lastAxisSelectTime > kAxisSelectDebounceMs) {
            m_lastAxisSelectTime = now;
            qDebug() << "AxisSelect Left   (LX =" << lx << ")";
            InputEvent event;
            event.type = InputEvent::Type::AxisSelect;
            event.axisDir = AxisSelectDirection::Left;
            emit inputEvent(event);
        }
    }

    // ──── 打印 RX/RY（保持 servoV6 启动后打印 RX/RY 的功能不变）────
    float rx = pad.rx();
    float ry = pad.ry();
    if (rx != 0.0f || ry != 0.0f) {
        qDebug() << "RX =" << rx << " RY =" << ry;
    }

    // ──── 右摇杆：运动事件（Step3 暂不做 Motion 语义解释，仅框架准备）────
    (void)ry;  // 后续 Step 再实现 Motion 解释
    (void)m_lastRYDir;

    // ──── 按钮事件（Step3 暂不做，仅边缘检测框架）────
    (void)m_lastA;
    (void)m_lastB;
    (void)m_lastX;
    (void)m_lastY;
}