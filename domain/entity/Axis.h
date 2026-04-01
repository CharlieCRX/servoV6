#ifndef AXIS_H
#define AXIS_H
#pragma once
#include <optional>
enum class Direction {
    Forward,
    Backward
};

enum class AxisState {
    Unknown,  // 对应 0: 未知状态
    Disabled, // 对应 1: 未使能
    Idle,     // 对应 2: 使能 (空闲)
    Jogging,  // 对应 3: 使能 (正在 Jog)
    Moving,   // 对应 4: 使能 (正在 Move)
    Error     // 对应 5: 报警
};

struct AxisFeedback {
    AxisState state;
};

class Axis {
public:
    Axis();

    AxisState state() const;

    void applyFeedback(const AxisFeedback& feedback);

    bool jog(Direction dir);

    bool hasPendingCommand() const;

    std::optional<Direction> pendingDirection() const;
private:
    AxisState m_state;
    std::optional<Direction> pending_direction_; // 可能存在的待执行的 Jog 方向
};
#endif // AXIS_H
