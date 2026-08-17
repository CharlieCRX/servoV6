#include "GamepadInputInterpreter.h"

#include "infrastructure/joystick/AndroidGamepadJoystick.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QThread>

GamepadInputInterpreter::GamepadInputInterpreter(QObject* parent)
    : QObject(parent)
{
}

void GamepadInputInterpreter::start()
{
    auto& pad = AndroidGamepadJoystick::instance();

    qDebug() << "[Interpreter] start thread=" << QThread::currentThread()
             << "isMain=" << (QThread::currentThread() == QCoreApplication::instance()->thread());
    qDebug() << "[Interpreter] joystickThread=" << pad.thread()
             << "interpreterThread=" << this->thread();

    const bool ok = connect(&pad, &AndroidGamepadJoystick::changed,
                            this, &GamepadInputInterpreter::onGamepadChanged);
    qDebug() << "[Interpreter] connect joystick changed -> onGamepadChanged:" << ok;

    if (!ok) {
        qWarning() << "[Interpreter] Signal-slot connection failed; input events disabled";
    } else {
        qDebug() << "[Interpreter] Signal-slot connection ok; listening";
    }

    qDebug() << "[Interpreter] deadzoneLX=" << kDeadzone
             << "deadzoneLY=" << kDeadzoneLY
             << "debounceMs=" << kAxisSelectDebounceMs;
}

static bool processAxisSelect(float value,
                              float deadzone,
                              bool& wasOutside,
                              qint64& lastTime,
                              qint64& sharedLastTime,
                              int debounceMs,
                              AxisSelectDirection dirWhenPositive,
                              AxisSelectDirection dirWhenNegative,
                              const char* tag,
                              GamepadInputInterpreter* self)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    auto emitAxis = [&](AxisSelectDirection dir) {
        InputEvent event;
        event.type = InputEvent::Type::AxisSelect;
        event.axisDir = dir;
        emit self->inputEvent(event);
    };

    if (value > +deadzone) {
        const qint64 elapsed = now - lastTime;
        const qint64 sharedElapsed = now - sharedLastTime;
        const auto dir = dirWhenPositive;
        qDebug() << "[Interpreter]" << tag << "value=" << value
                 << "side=positive"
                 << "dir=" << (dir == AxisSelectDirection::Right ? "Right" : "Left")
                 << "wasOutside=" << wasOutside
                 << "elapsedMs=" << elapsed
                 << "sharedElapsedMs=" << sharedElapsed;

        if (!wasOutside && elapsed > debounceMs && sharedElapsed > debounceMs) {
            lastTime = sharedLastTime = now;
            wasOutside = true;
            qDebug() << "[Interpreter] AxisSelect"
                     << (dir == AxisSelectDirection::Right ? "Right" : "Left")
                     << "via=" << tag << "value=" << value;
            emitAxis(dir);
            return true;
        }

        qDebug() << "[Interpreter] AxisSelect blocked tag=" << tag
                 << "wasOutside=" << wasOutside
                 << "elapsedMs=" << elapsed
                 << "sharedElapsedMs=" << sharedElapsed;
        wasOutside = true;
        return false;
    }

    if (value < -deadzone) {
        const qint64 elapsed = now - lastTime;
        const qint64 sharedElapsed = now - sharedLastTime;
        const auto dir = dirWhenNegative;
        qDebug() << "[Interpreter]" << tag << "value=" << value
                 << "side=negative"
                 << "dir=" << (dir == AxisSelectDirection::Right ? "Right" : "Left")
                 << "wasOutside=" << wasOutside
                 << "elapsedMs=" << elapsed
                 << "sharedElapsedMs=" << sharedElapsed;

        if (!wasOutside && elapsed > debounceMs && sharedElapsed > debounceMs) {
            lastTime = sharedLastTime = now;
            wasOutside = true;
            qDebug() << "[Interpreter] AxisSelect"
                     << (dir == AxisSelectDirection::Right ? "Right" : "Left")
                     << "via=" << tag << "value=" << value;
            emitAxis(dir);
            return true;
        }

        qDebug() << "[Interpreter] AxisSelect blocked tag=" << tag
                 << "wasOutside=" << wasOutside
                 << "elapsedMs=" << elapsed
                 << "sharedElapsedMs=" << sharedElapsed;
        wasOutside = true;
        return false;
    }

    if (wasOutside) {
        qDebug() << "[Interpreter]" << tag << "returned inside deadzone value=" << value
                 << "deadzone=" << deadzone;
        lastTime = now;
    }
    wasOutside = false;
    return false;
}

void GamepadInputInterpreter::onGamepadChanged()
{
    auto& pad = AndroidGamepadJoystick::instance();
    const float lx = pad.lx();
    const float ly = pad.ly();
    const float rx = pad.rx();
    const float ry = pad.ry();

    qDebug() << "[Interpreter] onGamepadChanged lx=" << lx
             << "ly=" << ly << "rx=" << rx << "ry=" << ry;

    processAxisSelect(ly, kDeadzoneLY,
                      m_lyWasOutside, m_lastLYSelectTime, m_lastAxisSelectTime,
                      kAxisSelectDebounceMs,
                      AxisSelectDirection::Right,
                      AxisSelectDirection::Left,
                      "LY", this);

    const bool lyInDeadzone = (ly >= -kDeadzoneLY && ly <= kDeadzoneLY);
    if (lyInDeadzone) {
        processAxisSelect(lx, kDeadzone,
                          m_lxWasOutside, m_lastLXSelectTime, m_lastAxisSelectTime,
                          kAxisSelectDebounceMs,
                          AxisSelectDirection::Right,
                          AxisSelectDirection::Left,
                          "LX", this);
    } else if (lx >= -kDeadzone && lx <= kDeadzone) {
        if (m_lxWasOutside) {
            qDebug() << "[Interpreter] LX reset while LY has priority. lx=" << lx;
        }
        m_lxWasOutside = false;
    }

    RYDirection newDir = RYDirection::Neutral;
    if (ry < -kDeadzoneRY) {
        newDir = RYDirection::Forward;
    } else if (ry > +kDeadzoneRY) {
        newDir = RYDirection::Backward;
    }

    if (newDir != m_lastRYDir) {
        if (m_lastRYDir == RYDirection::Forward) {
            qDebug() << "[Interpreter] Motion ForwardReleased ry=" << ry;
            InputEvent event;
            event.type = InputEvent::Type::Motion;
            event.motionDir = MotionDirection::Forward;
            event.motionType = MotionEventType::Released;
            emit inputEvent(event);
        } else if (m_lastRYDir == RYDirection::Backward) {
            qDebug() << "[Interpreter] Motion BackwardReleased ry=" << ry;
            InputEvent event;
            event.type = InputEvent::Type::Motion;
            event.motionDir = MotionDirection::Backward;
            event.motionType = MotionEventType::Released;
            emit inputEvent(event);
        }

        if (newDir == RYDirection::Forward) {
            qDebug() << "[Interpreter] Motion ForwardPressed ry=" << ry;
            InputEvent event;
            event.type = InputEvent::Type::Motion;
            event.motionDir = MotionDirection::Forward;
            event.motionType = MotionEventType::Pressed;
            emit inputEvent(event);
        } else if (newDir == RYDirection::Backward) {
            qDebug() << "[Interpreter] Motion BackwardPressed ry=" << ry;
            InputEvent event;
            event.type = InputEvent::Type::Motion;
            event.motionDir = MotionDirection::Backward;
            event.motionType = MotionEventType::Pressed;
            emit inputEvent(event);
        }

        m_lastRYDir = newDir;
    }

    (void)m_lastLX;
    (void)m_lastLY;
    (void)m_lastA;
    (void)m_lastB;
    (void)m_lastX;
    (void)m_lastY;
}
