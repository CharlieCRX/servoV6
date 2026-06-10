#include "AndroidGamepadJoystick.h"
#include <QMetaObject>

AndroidGamepadJoystick &AndroidGamepadJoystick::instance()
{
    static AndroidGamepadJoystick g;
    return g;
}

void AndroidGamepadJoystick::updateAxis(float lx, float ly, float rx, float ry, float lt, float rt)
{
    m_lx = lx;
    m_ly = ly;
    m_rx = rx;
    m_ry = ry;
    m_lt = lt;
    m_rt = rt;
    emit changed();
}

void AndroidGamepadJoystick::updateButton(int keyCode, bool pressed)
{
    switch (keyCode) {
    case 96:  m_a = pressed; break;
    case 97:  m_b = pressed; break;
    case 99:  m_x = pressed; break;
    case 100: m_y = pressed; break;
    default:  break;
    }
    emit changed();
}

#ifdef Q_OS_ANDROID
#include <jni.h>
#include <QDebug>
#include <QCoreApplication>

/// JNI 线程安全投递到主线程 event loop：
/// - 使用 QCoreApplication::instance() 作为投递目标（保证在主线程执行）
/// - 避免在 JNI 线程直接操作 QObject（跨线程 Q_PROPERTY 写入不安全）
extern "C" JNIEXPORT void JNICALL
Java_org_qtproject_gamepad_GamepadBridge_nativeAxisChanged(
    JNIEnv *, jclass,
    jfloat lx, jfloat ly, jfloat rx, jfloat ry, jfloat lt, jfloat rt)
{
    qDebug() << "[JNI] nativeAxisChanged  lx=" << lx << " ly=" << ly
             << " rx=" << rx << " ry=" << ry << " lt=" << lt << " rt=" << rt;
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [lx, ly, rx, ry, lt, rt]() {
            AndroidGamepadJoystick::instance().updateAxis(lx, ly, rx, ry, lt, rt);
        },
        Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL
Java_org_qtproject_gamepad_GamepadBridge_nativeButtonChanged(
    JNIEnv *, jclass, jint keyCode, jboolean pressed)
{
    qDebug() << "[JNI] nativeButtonChanged  keyCode=" << keyCode << " pressed=" << pressed;
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [keyCode, pressed]() {
            AndroidGamepadJoystick::instance().updateButton(keyCode, pressed);
        },
        Qt::QueuedConnection);
}
#endif // Q_OS_ANDROID
