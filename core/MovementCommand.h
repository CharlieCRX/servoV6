// core/MovementCommand.h
#ifndef MOVEMENT_COMMAND_H
#define MOVEMENT_COMMAND_H

#include <variant>
#include <vector>

// 定义各种命令的结构体
struct SetPositionSpeed {
    double mm_per_sec;
};

struct SetJogSpeed {
    double mm_per_sec; // 点动速度（毫米/秒）
};

struct RelativeMove {
    double delta_mm;
};

struct AbsoluteMove {
    double target_mm; // 目标绝对位置（毫米）
};

struct StartJog {
    double speed_mm_per_sec; // 点动速度
    bool positiveDirection;  // true 为正向，false 为负向
};
struct StopJog {}; // 无参数

struct Wait {
    long milliseconds;
};

struct GoHome {};  // 无参数

// 使用 std::variant 定义 Command 类型
using Command = std::variant<SetPositionSpeed, SetJogSpeed, RelativeMove, AbsoluteMove, StartJog, StopJog, Wait, GoHome>;


// 定义命令序列
using CommandSequence = std::vector<Command>;

#endif // MOVEMENT_COMMAND_H
