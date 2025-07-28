// core/MovementCommand.h (保持业务单位)
#ifndef MOVEMENT_COMMAND_H
#define MOVEMENT_COMMAND_H

#include <variant>
#include <vector>

// 现有命令结构体 (与之前的保持一致，都是业务单位)
struct SetPositionSpeed { double mm_per_sec; };
struct SetJogSpeed { double mm_per_sec; };
struct RelativeMove { double delta_mm; };
struct AbsoluteMove { double target_mm; };
struct StartPositiveJog {};
struct StartNegativeJog {};

struct SetAngularPositionSpeed { double degrees_per_sec; };
struct SetAngularJogSpeed { double degrees_per_sec; };
struct AngularMove { double degrees; };
struct StartPositiveAngularJog {};
struct StartNegativeAngularJog {};

struct StopJog {};
struct Wait { long milliseconds; };
struct GoHome {};

// Command variant 包含所有业务命令
using Command = std::variant<
    SetPositionSpeed, SetJogSpeed,
    SetAngularPositionSpeed, SetAngularJogSpeed,
    RelativeMove, AbsoluteMove,
    AngularMove,
    StartPositiveJog, StartNegativeJog,
    StartPositiveAngularJog, StartNegativeAngularJog,
    StopJog, Wait, GoHome
    >;

using CommandSequence = std::vector<Command>;

#endif // MOVEMENT_COMMAND_H
