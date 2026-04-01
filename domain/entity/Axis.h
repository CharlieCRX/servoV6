#ifndef AXIS_H
#define AXIS_H
#pragma once
enum class Direction {
    Forward,
    Backward
};

enum class AxisState {
    Unknown,  // 对应 0: 未知状态
    Disabled, // 对应 1: 未使能
    Idle,     // 对应 2: 使能 (空闲)
    Moving,   // 对应 3: 使能 (正在运动)
    Error     // 对应 4: 报警
};

struct AxisFeedback {
    AxisState state;
};

class Axis {
public:
    Axis();
    AxisState state() const;
    void applyFeedback(const AxisFeedback& feedback);
private:
    AxisState m_state;
};
#endif // AXIS_H
