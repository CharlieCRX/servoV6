#pragma once

#include "entity/AxisId.h"
#include "entity/Axis.h"
#include <unordered_map>
#include <stdexcept>

// 前向声明：IAxisDriver 是应用层抽象接口
// SystemContext 只持有原始指针，不依赖具体实现
class IAxisDriver;

/**
 * SystemContext = Group 的运行载体
 * 
 * 职责：
 * - 持有该分组下所有 Axis 实例
 * - 持有该分组绑定的 Driver（PLC 连接），通过抽象接口 IAxisDriver
 * - 持有该分组的全局状态（如龙门联动/解耦模式）
 * 
 * 关键约束（来自架构文档）：
 * 1. Axis（轴）不感知分组概念 —— Axis 不知道自己属于哪个 Group
 * 2. 不同 SystemContext 之间绝对隔离（平行宇宙）
 * 3. 龙门状态属于 SystemContext 的全局状态，不属于任何单个 Axis
 * 4. Axis 在 AxisRepository 中平级注册，身份的互斥由编排层 Policy 负责
 * 
 * 当前阶段包含（骨架）：
 * ✔ 实体全量注册（Y/Z/R/X/X1/X2 平等注册）
 * ✔ 全局状态锚点（m_isGantryCoupled 联动/解耦标志）
 * 
 * 当前阶段屏蔽（下阶段 Step 2 & 3 实现）：
 * ❌ FakePLC 防撕裂运算（abs(X1-X2) 超差检查）
 * ❌ GantrySafetyPolicy 安全拦截策略
 */
class SystemContext {
public:
    SystemContext() = default;

    // --- 轴注册（领域内自持 Axis 实例） ---

    /**
     * @brief 注册一个轴到当前分组
     * 在初始化时调用，注册所有轴实体（Y/Z/R/X/X1/X2）
     */
    void registerAxis(AxisId id) {
        m_axes.emplace(id, Axis{});
    }

    /**
     * @brief 便捷方法：注册该分组下所有标准轴（Y/Z/R/X/X1/X2）
     * 用于测试和快速初始化
     */
    void registerAllStandardAxes() {
        registerAxis(AxisId::Y);
        registerAxis(AxisId::Z);
        registerAxis(AxisId::R);
        registerAxis(AxisId::X);
        registerAxis(AxisId::X1);
        registerAxis(AxisId::X2);
    }

    // --- 轴访问 ---

    /**
     * @brief 获取已注册轴的引用
     * @throw std::out_of_range 如果 AxisId 未注册
     */
    Axis& getAxis(AxisId id) {
        auto it = m_axes.find(id);
        if (it == m_axes.end()) {
            throw std::out_of_range("SystemContext: Requested AxisId is not registered.");
        }
        return it->second;
    }

    // --- 驱动绑定 ---

    /**
     * @brief 绑定该分组的 IAxisDriver 抽象
     * 用于将 Axis 的命令意图发送到物理（或模拟）层
     */
    void setDriver(IAxisDriver* driver) {
        m_driver = driver;
    }

    IAxisDriver* driver() const {
        return m_driver;
    }

    // --- 龙门全局状态 ---

    /**
     * @brief 判断当前分组是否处于龙门联动模式
     * true  = Coupled（联动）：使用逻辑轴 X
     * false = Decoupled（解耦）：使用物理轴 X1/X2
     */
    bool isGantryCoupled() const {
        return m_isGantryCoupled;
    }

    /**
     * @brief 设置龙门联动/解耦模式
     * @param coupled true=联动, false=解耦
     * 
     * 业务约束（后续阶段强化）：
     * - 模式切换仅在 Idle 状态下允许（当前阶段暂不强制检查）
     * - 模式切换后，上层 Service 负责确保无运动指令积压
     */
    void setGantryCoupled(bool coupled) {
        m_isGantryCoupled = coupled;
    }

private:
    std::unordered_map<AxisId, Axis> m_axes;
    IAxisDriver* m_driver = nullptr;
    bool m_isGantryCoupled = true;  // 默认处于联动模式
};
