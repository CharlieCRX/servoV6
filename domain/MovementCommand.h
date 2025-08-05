#ifndef MOVEMENT_COMMAND_H
#define MOVEMENT_COMMAND_H

#include <variant>
#include <vector>
// 此文件负责定义所有命令的业务单位结构体
// 线性运动命令结构体
struct SetPositionSpeed { double mm_per_sec; };
struct SetJogSpeed { double mm_per_sec; };
struct RelativeMove { double delta_mm; };
struct AbsoluteMove { double target_mm; };
struct StartPositiveJog {};
struct StartNegativeJog {};

// 旋转运动命令结构体
struct SetAngularPositionSpeed { double degrees_per_sec; };
struct SetAngularJogSpeed { double degrees_per_sec; };
struct RelativeAngularMove { double degrees; }; // 新增：相对旋转
struct AbsoluteAngularMove { double degrees; }; // 新增：绝对旋转
struct StartPositiveAngularJog {};
struct StartNegativeAngularJog {};

// 通用命令结构体
struct StopJog {};
struct Wait { long milliseconds; };
struct GoHome {};
struct InitEnvironment {}; // 新增：初始化环境命令
struct EmergencyStop {};  // 新增：急停命令

// Command variant 包含所有业务命令
using Command = std::variant<
    SetPositionSpeed, SetJogSpeed,
    SetAngularPositionSpeed, SetAngularJogSpeed,
    RelativeMove, AbsoluteMove,
    RelativeAngularMove, AbsoluteAngularMove,
    StartPositiveJog, StartNegativeJog,
    StartPositiveAngularJog, StartNegativeAngularJog,
    StopJog, Wait, GoHome, InitEnvironment, EmergencyStop
    >;

using CommandSequence = std::vector<Command>;

#endif // MOVEMENT_COMMAND_H
