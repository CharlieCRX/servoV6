#include "BusinessLogic.h"
#include "MotorCommandExecutor.h"
#include "IServoAdapter.h"
#include "Logger.h"

// 构造函数现在接收适配器 map
BusinessLogic::BusinessLogic(std::map<std::string, std::unique_ptr<IServoAdapter>> adapters)
    : adapterMap(std::move(adapters)) {
    LOG_INFO("业务逻辑层初始化，包含 {} 个电机适配器。", adapterMap.size());
}

BusinessLogic::~BusinessLogic() {
    LOG_INFO("业务逻辑层正在关闭。");
}

bool BusinessLogic::executeCommandSequence(const std::string& motorId, const CommandSequence& commands) {
    auto it = adapterMap.find(motorId);
    if (it == adapterMap.end()) {
        LOG_ERROR("执行命令序列失败：未找到电机 '{}' 的适配器。", motorId);
        return false;
    }

    IServoAdapter* adapter = it->second.get();
    MotorCommandExecutor executor;

    LOG_INFO("开始为电机 '{}' 执行命令序列 (共 {} 条命令)。", motorId, commands.size());

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
                    LOG_ERROR("命令 '{}' (线性) 不适用于适配器 '{}' (非线性适配器)。", typeid(T).name(), motorId);
                    return false;
                }
            }
            // 旋转命令
            else if constexpr (std::is_same_v<T, SetAngularPositionSpeed> ||
                               std::is_same_v<T, SetAngularJogSpeed> ||
                               std::is_same_v<T, RelativeAngularMove> ||
                               std::is_same_v<T, AbsoluteAngularMove> ||
                               std::is_same_v<T, StartPositiveAngularJog> ||
                               std::is_same_v<T, StartNegativeAngularJog>)
            {
                if (IRotaryServoAdapter* rotaryAdapter = dynamic_cast<IRotaryServoAdapter*>(adapter)) {
                    return executor.visit(rotaryAdapter, arg);
                } else {
                    LOG_ERROR("命令 '{}' (旋转) 不适用于适配器 '{}' (非旋转适配器)。", typeid(T).name(), motorId);
                    return false;
                }
            }
            // 通用命令
            else if constexpr (std::is_same_v<T, Wait> ||
                               std::is_same_v<T, GoHome> ||
                               std::is_same_v<T, StopJog> ||
                               std::is_same_v<T, InitEnvironment> ||
                               std::is_same_v<T, EmergencyStop>)
            {
                return executor.visit(adapter, arg); // 通用命令直接传给 IServoAdapter*
            }
            // 未知命令
            else {
                LOG_ERROR("适配器 '{}' 遇到不支持的命令类型 '{}'。", motorId, typeid(T).name());
                return false;
            }
        }, cmd);

        if (!success) {
            LOG_WARN("适配器 '{}' 的命令序列执行失败，中止后续命令。", motorId);
            return false;
        }
    }
    LOG_INFO("适配器 '{}' 的命令序列成功执行。", motorId);
    return true;
}
