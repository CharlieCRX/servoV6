// core/BusinessLogic.cpp
#include "BusinessLogic.h"
// 不再需要 <iostream>，因为我们将使用 Logger
// #include <iostream>    // 用于演示日志
#include <variant>     // 用于 std::visit
#include <Logger.h> // 包含我们的 Logger 头文件

BusinessLogic::BusinessLogic(std::map<std::string, std::unique_ptr<IMotor>> motors)
    : motorMap(std::move(motors)) {
    // 构造函数中可以添加日志，表明业务逻辑实例被创建
    LOG_DEBUG("BusinessLogic instance created with {} motors.", motorMap.size());
}

BusinessLogic::~BusinessLogic() {
    // unique_ptr 会自动清理
    LOG_DEBUG("BusinessLogic instance destroyed.");
}

bool BusinessLogic::executeCommandSequence(const std::string& motorId, const CommandSequence& commands) {
    auto it = motorMap.find(motorId);
    if (it == motorMap.end()) {
        // 使用 LOG_ERROR 替代 std::cerr
        LOG_ERROR("Motor '{}' not found for command sequence execution.", motorId);
        return false;
    }

    IMotor* motor = it->second.get();
    bool success = true; // 用于跟踪命令执行是否成功

    LOG_INFO("Executing command sequence for motor '{}' ({} commands).", motorId, commands.size());

    for (const auto& cmd : commands) {
        // 使用 std::visit 处理不同的命令类型
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, SetSpeed>) {
                // 使用 LOG_DEBUG 替代 std::cout
                LOG_DEBUG("Setting speed for motor '{}' to {} mm/s", motorId, arg.mm_per_sec);
                if (!motor->setSpeed(arg.mm_per_sec)) {
                    // 使用 LOG_ERROR 替代 std::cerr
                    LOG_ERROR("Failed to set speed for motor '{}'.", motorId);
                    success = false;
                }
            } else if constexpr (std::is_same_v<T, RelativeMove>) {
                // 使用 LOG_DEBUG 替代 std::cout
                LOG_DEBUG("Moving motor '{}' by {} mm", motorId, arg.delta_mm);
                if (!motor->relativeMove(arg.delta_mm)) {
                    // 使用 LOG_ERROR 替代 std::cerr
                    LOG_ERROR("Failed to move motor '{}'.", motorId);
                    success = false;
                }
            } else if constexpr (std::is_same_v<T, Wait>) {
                // 使用 LOG_DEBUG 替代 std::cout
                LOG_DEBUG("Waiting for {} ms...", arg.milliseconds);
                motor->wait(arg.milliseconds); // IMotor 已经提供了 wait 方法
            } else if constexpr (std::is_same_v<T, GoHome>) {
                // 使用 LOG_DEBUG 替代 std::cout
                LOG_DEBUG("Homing motor '{}'...", motorId);
                if (!motor->goHome()) {
                    // 使用 LOG_ERROR 替代 std::cerr
                    LOG_ERROR("Failed to home motor '{}'.", motorId);
                    success = false;
                }
            }
        }, cmd);

        // 如果在处理当前命令时发生了错误，则立即停止并返回 false
        if (!success) {
            LOG_WARN("Command execution failed for motor '{}'. Aborting sequence.", motorId);
            return false;
        }
    }
    LOG_INFO("Command sequence executed successfully for motor '{}'.", motorId);
    return true; // 所有命令都成功执行
}
