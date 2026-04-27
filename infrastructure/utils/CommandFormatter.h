#pragma once
#include "../../domain/entity/Axis.h"
#include <string>
#include <variant>
#include <type_traits>

namespace utils {

// 🌟 使用 inline 保证在多个源文件引入时不会引发重复定义错误
inline std::string format(const AxisCommand& cmd) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        
        if constexpr (std::is_same_v<T, std::monostate>) {
            return "Empty Command (Monostate)";
        } 
        else if constexpr (std::is_same_v<T, EnableCommand>) {
            return "EnableCommand(active=" + std::string(arg.active ? "true" : "false") + ")";
        } 
        else if constexpr (std::is_same_v<T, JogCommand>) {
            std::string dir = (arg.dir == Direction::Forward) ? "Forward" : "Backward";
            return "JogCommand(dir=" + dir + ", active=" + std::string(arg.active ? "true" : "false") + ")";
        } 
        else if constexpr (std::is_same_v<T, MoveCommand>) {
            std::string type = (arg.type == MoveType::Absolute) ? "Absolute" : 
                               (arg.type == MoveType::Relative) ? "Relative" : "None";
            return "MoveCommand(type=" + type + ", target=" + std::to_string(arg.target) + ")";
        } 
        else if constexpr (std::is_same_v<T, StopCommand>) {
            return "StopCommand()";
        } 
        else if constexpr (std::is_same_v<T, ZeroAbsoluteCommand>) {
            return "ZeroAbsoluteCommand()";
        } 
        else if constexpr (std::is_same_v<T, SetRelativeZeroCommand>) {
            return "SetRelativeZeroCommand()";
        } 
        else if constexpr (std::is_same_v<T, ClearRelativeZeroCommand>) {
            return "ClearRelativeZeroCommand()";
        } 
        else if constexpr (std::is_same_v<T, SetJogVelocityCommand>) {
            return "SetJogVelocityCommand(velocity=" + std::to_string(arg.velocity) + ")";
        } 
        else if constexpr (std::is_same_v<T, SetMoveVelocityCommand>) {
            return "SetMoveVelocityCommand(velocity=" + std::to_string(arg.velocity) + ")";
        } 
        else {
            return "UnknownCommand";
        }
    }, cmd);
}

} // namespace utils