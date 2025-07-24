// core/BusinessLogic.cpp
#include "BusinessLogic.h"
#include <iostream>   // 用于演示日志
#include <variant>    // 用于 std::visit

BusinessLogic::BusinessLogic(std::map<std::string, std::unique_ptr<IMotor>> motors)
    : motorMap(std::move(motors)) {
}

BusinessLogic::~BusinessLogic() {
    // unique_ptr 会自动清理
}

bool BusinessLogic::executeCommandSequence(const std::string& motorId, const CommandSequence& commands) {
    auto it = motorMap.find(motorId);
    if (it == motorMap.end()) {
        std::cerr << "Error: Motor '" << motorId << "' not found." << std::endl;
        return false;
    }

    IMotor* motor = it->second.get();
    bool success = true; // 用于跟踪命令执行是否成功

    for (const auto& cmd : commands) {
        // 使用 std::visit 处理不同的命令类型
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, SetSpeed>) {
                std::cout << "DEBUG: Setting speed for motor '" << motorId << "' to " << arg.mm_per_sec << " mm/s" << std::endl;
                if (!motor->setSpeed(arg.mm_per_sec)) {
                    std::cerr << "ERROR: Failed to set speed for motor '" << motorId << "'." << std::endl;
                    success = false;
                }
            } else if constexpr (std::is_same_v<T, RelativeMove>) {
                std::cout << "DEBUG: Moving motor '" << motorId << "' by " << arg.delta_mm << " mm" << std::endl;
                if (!motor->relativeMove(arg.delta_mm)) {
                    std::cerr << "ERROR: Failed to move motor '" << motorId << "'." << std::endl;
                    success = false;
                }
            } else if constexpr (std::is_same_v<T, Wait>) {
                std::cout << "DEBUG: Waiting for " << arg.milliseconds << " ms..." << std::endl;
                motor->wait(arg.milliseconds); // IMotor 已经提供了 wait 方法
            } else if constexpr (std::is_same_v<T, GoHome>) {
                std::cout << "DEBUG: Homing motor '" << motorId << "'..." << std::endl;
                if (!motor->goHome()) {
                    std::cerr << "ERROR: Failed to home motor '" << motorId << "'." << std::endl;
                    success = false;
                }
            }
        }, cmd);

        // 如果在处理当前命令时发生了错误，则立即停止并返回 false
        if (!success) {
            return false;
        }
    }
    return true; // 所有命令都成功执行
}
