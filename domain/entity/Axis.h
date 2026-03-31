#ifndef AXIS_H
#define AXIS_H
#pragma once
enum class Direction {
    Forward,
    Backward
};

enum class AxisState {
    Disabled, // 对应 0: 未使能
    Idle,     // 对应 1: 使能 (空闲)
    Moving,   // 对应 2: 使能 (正在运动)
    Error     // 对应 3: 报警
};

class Axis {
public:
    AxisState state() const;
};
#endif // AXIS_H
