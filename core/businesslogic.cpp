// core/BusinessLogic.cpp
#include "BusinessLogic.h"
#include "MotorCommandExecutor.h"
#include "IServoAdapter.h" // 包含所有适配器接口
#include "Logger.h"

// 构造函数现在接收适配器 map
BusinessLogic::BusinessLogic(std::map<std::string, std::unique_ptr<IServoAdapter>> adapters)
    : adapterMap(std::move(adapters)) {
    LOG_INFO("BusinessLogic initialized with {} motor adapters.", adapterMap.size());
}

BusinessLogic::~BusinessLogic() {
    LOG_INFO("BusinessLogic shutting down.");
}

bool BusinessLogic::executeCommandSequence(const std::string& motorId, const CommandSequence& commands) {
    auto it = adapterMap.find(motorId);
    if (it == adapterMap.end()) {
        LOG_ERROR("Motor adapter '{}' not found for command sequence execution.", motorId);
        return false;
    }

    IServoAdapter* adapter = it->second.get(); // 获取基类适配器指针
    MotorCommandExecutor executor;

    LOG_INFO("Executing command sequence for motor '{}' ({} commands).", motorId, commands.size());

    for (const auto& cmd : commands) {
        bool success = std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;

            // 尝试将通用适配器指针向下转型到具体类型
            // 线性命令
            if constexpr (std::is_same_v<T, SetPositionSpeed> ||
                          std::is_same_v<T, SetJogSpeed> ||
                          std::is_same_v<T, RelativeMove> ||
                          std::is_same_v<T, AbsoluteMove> ||
                          std::is_same_v<T, StartPositiveJog> ||
                          std::is_same_v<T, StartNegativeJog>)
            {
                if (ILinearServoAdapter* linearAdapter = dynamic_cast<ILinearServoAdapter*>(adapter)) {
                    return executor.visit(linearAdapter, arg);
                } else {
                    LOG_ERROR("Command '{}' (linear) is not applicable to adapter '{}' (not a linear adapter).", typeid(T).name(), motorId);
                    return false;
                }
            }
            // 角度命令
            else if constexpr (std::is_same_v<T, SetAngularPositionSpeed> ||
                               std::is_same_v<T, SetAngularJogSpeed> ||
                               std::is_same_v<T, AngularMove> ||
                               std::is_same_v<T, StartPositiveAngularJog> ||
                               std::is_same_v<T, StartNegativeAngularJog>)
            {
                if (IRotaryServoAdapter* rotaryAdapter = dynamic_cast<IRotaryServoAdapter*>(adapter)) {
                    return executor.visit(rotaryAdapter, arg);
                } else {
                    LOG_ERROR("Command '{}' (angular) is not applicable to adapter '{}' (not a rotary adapter).", typeid(T).name(), motorId);
                    return false;
                }
            }
            // 通用命令
            else if constexpr (std::is_same_v<T, Wait> ||
                               std::is_same_v<T, GoHome> ||
                               std::is_same_v<T, StopJog>)
            {
                return executor.visit(adapter, arg); // 通用命令直接传给 IServoAdapter*
            }
            // 未知命令
            else {
                LOG_ERROR("Unsupported command type '{}' encountered for adapter '{}'.", typeid(T).name(), motorId);
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
