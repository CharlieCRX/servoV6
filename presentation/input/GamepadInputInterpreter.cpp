#include "GamepadInputInterpreter.h"
#include "infrastructure/joystick/AndroidGamepadJoystick.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDateTime>
#include <QThread>

GamepadInputInterpreter::GamepadInputInterpreter(QObject* parent)
    : QObject(parent)
{
}

void GamepadInputInterpreter::start()
{
    auto& pad = AndroidGamepadJoystick::instance();

    qDebug() << "[Interpreter] start() — thread=" << QThread::currentThread()
             << "isMain=" << (QThread::currentThread() == QCoreApplication::instance()->thread());
    qDebug() << "[Interpreter] singleton thread=" << pad.thread()
             << "interpreter thread=" << this->thread();

    bool ok = connect(&pad, &AndroidGamepadJoystick::changed,
                      this, &GamepadInputInterpreter::onGamepadChanged);
    qDebug() << "[Interpreter] connect(AndroidGamepadJoystick::changed -> onGamepadChanged) returned" << ok;

    if (!ok) {
        qWarning() << "[Interpreter] ❌ Signal-slot connection FAILED! Axis selection will not work.";
    } else {
        qDebug() << "[Interpreter] ✅ Signal-slot connection SUCCESS. Listening...";
    }

    qDebug() << "[Interpreter] deadzone LX=" << kDeadzone << " LY=" << kDeadzoneLY
             << " debounce=" << kAxisSelectDebounceMs << "ms (long-press auto-repeat)";
}

// ─── 辅助：死区边缘检测（单次触发，必须回中才能再次触发） ───
// 返回值：true 表示已 emit AxisSelect 事件
static bool processAxisSelect(
    float value, float deadzone, bool& wasOutside,
    qint64& lastTime, qint64& sharedLastTime, int debounceMs,
    AxisSelectDirection dirWhenPositive,  // value > +deadzone 时发出此方向
    AxisSelectDirection dirWhenNegative,  // value < -deadzone 时发出此方向
    const char* tag, GamepadInputInterpreter* self)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (value > +deadzone) {
        // 在正死区外
        qint64 elapsed = now - lastTime;
        qint64 sharedElapsed = now - sharedLastTime;
        qDebug() << "[Interpreter]" << tag << "=" << value << " > +deadzone(" << deadzone
                 << ") → dir=" << (dirWhenPositive == AxisSelectDirection::Right ? "Right" : "Left")
                 << "  wasOutside=" << wasOutside << " elapsed=" << elapsed << "ms"
                 << " sharedElapsed=" << sharedElapsed << "ms";

        if (!wasOutside && elapsed > debounceMs && sharedElapsed > debounceMs) {
            // 首次离开死区 → 立即触发（边缘检测，必须之前在中位）
            // ★ 同时刷新本轴时间和全局共享时间，防止斜推时另一个轴紧随触发
            lastTime = sharedLastTime = now;
            wasOutside = true;
            qDebug() << "[Interpreter] ✅ AxisSelect" << (dirWhenPositive == AxisSelectDirection::Right ? "Right" : "Left")
                     << " (via" << tag << ")  value=" << value;
            InputEvent event;
            event.type = InputEvent::Type::AxisSelect;
            event.axisDir = dirWhenPositive;
            emit self->inputEvent(event);
            return true;
        } else {
            qDebug() << "[Interpreter] ❌ blocked" << tag
                     << " (wasOutside=" << wasOutside << " elapsed=" << elapsed << "ms"
                     << " sharedElapsed=" << sharedElapsed << "ms)";
            // 保持 wasOutside=true，不触发直到回中
            wasOutside = true;
            return false;
        }
    } else if (value < -deadzone) {
        // 在负死区外
        qint64 elapsed = now - lastTime;
        qint64 sharedElapsed = now - sharedLastTime;
        qDebug() << "[Interpreter]" << tag << "=" << value << " < -deadzone(" << -deadzone
                 << ") → dir=" << (dirWhenNegative == AxisSelectDirection::Right ? "Right" : "Left")
                 << "  wasOutside=" << wasOutside << " elapsed=" << elapsed << "ms"
                 << " sharedElapsed=" << sharedElapsed << "ms";

        if (!wasOutside && elapsed > debounceMs && sharedElapsed > debounceMs) {
            // 首次离开死区 → 立即触发
            lastTime = sharedLastTime = now;
            wasOutside = true;
            qDebug() << "[Interpreter] ✅ AxisSelect" << (dirWhenNegative == AxisSelectDirection::Right ? "Right" : "Left")
                     << " (via" << tag << ")  value=" << value;
            InputEvent event;
            event.type = InputEvent::Type::AxisSelect;
            event.axisDir = dirWhenNegative;
            emit self->inputEvent(event);
            return true;
        } else {
            qDebug() << "[Interpreter] ❌ blocked" << tag
                     << " (wasOutside=" << wasOutside << " elapsed=" << elapsed << "ms"
                     << " sharedElapsed=" << sharedElapsed << "ms)";
            wasOutside = true;
            return false;
        }
    } else {
        // 回到死区内 → 重置边缘检测
        // ★ 刷新 lastTime，防止摇杆弹簧回弹越过中位进入反向死区导致误触发
        //   回弹时 elapsed = now - lastTime 接近 0，必定 < debounceMs，被拒绝
        if (wasOutside) {
            qDebug() << "[Interpreter]" << tag << "=" << value << " → returned inside deadzone (±" << deadzone
                     << ") — ready for next trigger";
            lastTime = now;
        }
        wasOutside = false;
        return false;
    }
}

void GamepadInputInterpreter::onGamepadChanged()
{
    auto& pad = AndroidGamepadJoystick::instance();
    float lx = pad.lx();
    float ly = pad.ly();
    float rx = pad.rx();
    float ry = pad.ry();

    qDebug() << "[Interpreter] 🔔 onGamepadChanged()  lx=" << lx << " ly=" << ly
             << " rx=" << rx << " ry=" << ry;

    // ═══════════════════════════════════════════════════════
    // LY 上下：向下推 → LY 正值（切换下一个轴），向上推 → LY 负值（切换上一个轴）
    //   实际数据：向下推 → LY 正值（+1.0），向上推 → LY 负值（-1.0）
    //   ★ LY 优先：先处理 LY，仅当 LY 在中位时（±kDeadzoneLY 内）才允许 LX 选轴
    // ═══════════════════════════════════════════════════════
    bool lyTriggered = processAxisSelect(
        ly, kDeadzoneLY, m_lyWasOutside, m_lastLYSelectTime, m_lastAxisSelectTime, kAxisSelectDebounceMs,
        AxisSelectDirection::Right,   // 正值 LY（向下推） → Right（下一个轴）
        AxisSelectDirection::Left,    // 负值 LY（向上推） → Left（上一个轴）
        "LY", this);

    // ═══════════════════════════════════════════════════════
    // LX 左右：LX > +0.15 → Right，LX < -0.15 → Left
    //   仅当 LY 在中位时才处理（避免斜推时 LX/LY 互相干扰导致"切了又回来"）
    // ═══════════════════════════════════════════════════════
    bool lyInDeadzone = (ly >= -kDeadzoneLY && ly <= kDeadzoneLY);
    if (lyInDeadzone) {
        processAxisSelect(lx, kDeadzone, m_lxWasOutside, m_lastLXSelectTime, m_lastAxisSelectTime, kAxisSelectDebounceMs,
                          AxisSelectDirection::Right,   // 正值 LX → Right
                          AxisSelectDirection::Left,    // 负值 LX → Left
                          "LX", this);
    } else {
        // LY 在死区外，跳过 LX 处理。但需要更新 LX 的回中状态
        if (lx >= -kDeadzone && lx <= kDeadzone) {
            if (m_lxWasOutside) {
                qDebug() << "[Interpreter] LX=" << lx << " → returned inside deadzone (±" << kDeadzone
                         << ") — LY priority, LX reset";
            }
            m_lxWasOutside = false;
        }
    }

    // ═══════════════════════════════════════════════════════
    // RY 上下：上推 → RY 负值（Forward），下推 → RY 正值（Backward）
    //   边缘触发：进入死区外 → emit Motion Pressed；回到死区内 → emit Motion Released
    // ═══════════════════════════════════════════════════════
    {
        RYDirection newDir = RYDirection::Neutral;
        if (ry < -kDeadzoneRY) {
            newDir = RYDirection::Forward;
        } else if (ry > +kDeadzoneRY) {
            newDir = RYDirection::Backward;
        }

        if (newDir != m_lastRYDir) {
            // ── 离开旧状态 → 先发 Released（如果有旧方向）──
            if (m_lastRYDir == RYDirection::Forward) {
                qDebug() << "[Interpreter] ✅ Motion ForwardReleased (RY=" << ry << ")";
                InputEvent event;
                event.type = InputEvent::Type::Motion;
                event.motionDir = MotionDirection::Forward;
                event.motionType = MotionEventType::Released;
                emit inputEvent(event);
            } else if (m_lastRYDir == RYDirection::Backward) {
                qDebug() << "[Interpreter] ✅ Motion BackwardReleased (RY=" << ry << ")";
                InputEvent event;
                event.type = InputEvent::Type::Motion;
                event.motionDir = MotionDirection::Backward;
                event.motionType = MotionEventType::Released;
                emit inputEvent(event);
            }

            // ── 进入新状态 → 发 Pressed（如果是方向）──
            if (newDir == RYDirection::Forward) {
                qDebug() << "[Interpreter] ✅ Motion ForwardPressed (RY=" << ry << ")";
                InputEvent event;
                event.type = InputEvent::Type::Motion;
                event.motionDir = MotionDirection::Forward;
                event.motionType = MotionEventType::Pressed;
                emit inputEvent(event);
            } else if (newDir == RYDirection::Backward) {
                qDebug() << "[Interpreter] ✅ Motion BackwardPressed (RY=" << ry << ")";
                InputEvent event;
                event.type = InputEvent::Type::Motion;
                event.motionDir = MotionDirection::Backward;
                event.motionType = MotionEventType::Pressed;
                emit inputEvent(event);
            }

            m_lastRYDir = newDir;
        }
    }

    // ──── 未实现的事件 ────
    (void)m_lastLX;
    (void)m_lastLY;
    (void)m_lastA;
    (void)m_lastB;
    (void)m_lastX;
    (void)m_lastY;
}