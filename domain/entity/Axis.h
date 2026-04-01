#ifndef AXIS_H
#define AXIS_H
#pragma once
#include <optional>
enum class Direction {
    Forward,
    Backward
};

enum class MoveType {
    None,
    Absolute,
    Relative
};

enum class AxisState {
    Unknown,        // 对应 0: 未知状态
    Disabled,       // 对应 1: 未使能

    // 以下状态都是使能状态的子状态
    Idle,           // 对应 2: 空闲
    Jogging,        // 对应 3: 正在 Jog
    MovingAbsolute, // 对应 4: 正在绝对定位
    MovingRelative, // 对应 5: 正在相对定位
    Error           // 对应 6: 报警
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
    bool moveAbsolute(double target);
    bool moveRelative(double distance);


    bool hasPendingCommand() const;
    std::optional<Direction> pendingDirection() const;
    MoveType pendingMoveType() const;
    std::optional<double> pendingTarget() const;
private:
    AxisState m_state;
    std::optional<Direction> m_pending_direction;   // 可能存在的待执行的 Jog 方向
    MoveType m_pending_move_type = MoveType::None;  // 当前待执行的 Move 类型
    std::optional<double> m_pending_target;         // 可能存在的待执行的 Move 目标（绝对位置或相对距离）
};
#endif // AXIS_H
