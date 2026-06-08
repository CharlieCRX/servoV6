#include "AndroidGamepadJoystick.h"

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