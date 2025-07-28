// core/BusinessLogic.cpp
#include "BusinessLogic.h"
#include "MotorCommandExecutor.h" // <-- 包含新的访问者实现
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
    MotorCommandExecutor executor; // 创建访问者实例

    LOG_INFO("Executing command sequence for motor '{}' ({} commands).", motorId, commands.size());

    for (const auto& cmd : commands) {
        // 使用 std::visit 调用访问者的相应方法
        // lambda 表达式作为 std::visit 的访问器，内部再调用 executor.visit
        bool success = std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            // 这段静态 if 仍然必要，因为它处理的是 std::variant 中所有可能的类型
            // 并且将具体的命令传递给 executor 的 visit 方法
            if constexpr (std::is_same_v<T, SetPositionSpeed> ||
                          std::is_same_v<T, SetJogSpeed> ||
                          std::is_same_v<T, RelativeMove> ||
                          std::is_same_v<T, Wait> ||
                          std::is_same_v<T, GoHome> ||
                          std::is_same_v<T, AbsoluteMove> ||
                          std::is_same_v<T, StartPositiveJog> ||
                          std::is_same_v<T, StartNegativeJog> ||
                          std::is_same_v<T, StopJog>)
            {
                // 将具体的命令结构体传递给访问者处理
                return executor.visit(motor, arg);
            } else {
                // 处理未知的命令类型（虽然理论上 Command 包含所有已知类型）
                LOG_ERROR("Unsupported command type encountered in CommandSequence for motor '{}'.", motorId);
                return false;
            }
        }, cmd);

        if (!success) {
            LOG_WARN("Command execution failed for motor '{}'. Aborting sequence.", motorId);
            return false;
        }
    }
    LOG_INFO("Command sequence executed successfully for motor '{}'.", motorId);
    return true;
}
