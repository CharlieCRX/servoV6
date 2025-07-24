// core/MovementCommand.h
#ifndef MOVEMENT_COMMAND_H
#define MOVEMENT_COMMAND_H

#include <variant>
#include <vector>

// 定义各种命令的结构体
struct SetSpeed {
    double mm_per_sec;
};

struct RelativeMove {
    double delta_mm;
};

struct Wait {
    long milliseconds;
};

struct GoHome {};  // 无参数

// 使用 std::variant 定义 Command 类型
using Command = std::variant<SetSpeed, RelativeMove, Wait, GoHome>;

// 定义命令序列
using CommandSequence = std::vector<Command>;

#endif // MOVEMENT_COMMAND_H
