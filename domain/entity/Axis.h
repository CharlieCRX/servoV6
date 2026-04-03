#ifndef AXIS_H
#define AXIS_H
#pragma once
#include <optional>
#include <variant>
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
    double absPos;
};


struct JogCommand {
    Direction dir;
};

struct MoveCommand {
    MoveType type;   // Absolute 或 Relative
    double target;   // 目标位置或距离
};

struct StopCommand {};

// 2. 更新统一意图槽位
using AxisCommand = std::variant<
    std::monostate, 
    JogCommand, 
    MoveCommand, 
    StopCommand
>;

class Axis {
public:
    Axis();

    AxisState state() const;

    void applyFeedback(const AxisFeedback& feedback);

    bool jog(Direction dir);
    bool moveAbsolute(double target);
    bool moveRelative(double distance);

    bool stop();

    // 状态查询接口
    double currentAbsolutePosition() const;


    bool hasPendingCommand() const;
    const AxisCommand& getPendingCommand() const;

    bool hasPendingStop() const;
private:
    AxisState m_state;
    // 唯一的命令意图
    AxisCommand m_pending_intent = std::monostate{};
    
    double m_current_abs_pos = 0.0;
};
#endif // AXIS_H
