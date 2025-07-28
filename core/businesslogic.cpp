// core/BusinessLogic.cpp
#include "BusinessLogic.h"
#include <variant>     // 用于 std::visit
#include <Logger.h>

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

                LOG_DEBUG("Setting speed for motor '{}' to {} mm/s", motorId, arg.mm_per_sec);

                if (!motor->setSpeed(arg.mm_per_sec)) {
                    LOG_ERROR("Failed to set speed for motor '{}'.", motorId);
                    success = false;
                }
            } else if constexpr (std::is_same_v<T, RelativeMove>) {
                LOG_DEBUG("Moving motor '{}' by {} mm", motorId, arg.delta_mm);
                if (!motor->relativeMove(arg.delta_mm)) {

                    LOG_ERROR("Failed to move motor '{}'.", motorId);
                    success = false;
                }
            } else if constexpr (std::is_same_v<T, AbsoluteMove>) {
                LOG_DEBUG("Moving motor '{}' to absolute position {} mm.", motorId, arg.target_mm);
                if (!motor->absoluteMove(arg.target_mm)) {
                    LOG_ERROR("Failed to move motor '{}' to absolute position {}.", motorId, arg.target_mm);
                    success = false;
                }
            }  else if constexpr (std::is_same_v<T, StartJog>) {
                const char* direction = arg.positiveDirection ? "positive" : "negative";
                LOG_DEBUG("Starting jog for motor '{}' in {} direction at {} mm/s.", motorId, direction, arg.speed_mm_per_sec);
                if (!motor->startJog(arg.speed_mm_per_sec, arg.positiveDirection)) {
                    LOG_ERROR("Failed to start jog for motor '{}' in {} direction.", motorId, direction);
                    success = false;
                }
            } else if constexpr (std::is_same_v<T, StopJog>) {
                LOG_DEBUG("Stopping jog for motor '{}'.", motorId);
                if (!motor->stopJog()) {
                    LOG_ERROR("Failed to stop jog for motor '{}'.", motorId);
                    success = false;
                }
            } else if constexpr (std::is_same_v<T, Wait>) {

                LOG_DEBUG("Waiting for {} ms...", arg.milliseconds);
                motor->wait(arg.milliseconds); // IMotor 已经提供了 wait 方法
            } else if constexpr (std::is_same_v<T, GoHome>) {

                LOG_DEBUG("Homing motor '{}'...", motorId);
                if (!motor->goHome()) {

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
