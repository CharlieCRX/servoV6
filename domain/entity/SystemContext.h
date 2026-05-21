// domain/entity/SystemContext.h
#pragma once
#include <memory>
#include "entity/Axis.h"
#include "entity/AxisId.h"
#include "entity/ContextRejection.h"
#include "gantry/GantryCouplingController.h"
#include "gantry/GantryPowerController.h"
#include "safety/EmergencyStopController.h"
#include "infrastructure/ISystemDriver.h"
#include "infrastructure/logger/Logger.h"
#include <unordered_map>
#include <sstream>

class SystemContext {
public:
    SystemContext() {
        // 1. 初始化 6 个固定轴实体
        m_axes[AxisId::X] = std::make_unique<Axis>();
        m_axes[AxisId::X1] = std::make_unique<Axis>();
        m_axes[AxisId::X2] = std::make_unique<Axis>();
        m_axes[AxisId::Y] = std::make_unique<Axis>();
        m_axes[AxisId::Z] = std::make_unique<Axis>();
        m_axes[AxisId::R] = std::make_unique<Axis>();

        // 2. 初始化龙门联动控制器（不持有 Axis 引用，PLC 负责物理安全校验）
        m_gantryCouplingController = std::make_unique<GantryCouplingController>();

        // 3. 初始化龙门电机控制器（独立于联动状态，任何状态下均可访问）
        m_gantryPowerController = std::make_unique<GantryPowerController>();
    }

    /**
     * @brief Try-Get 模式获取轴对象（控制操作入口）
     * @param id 目标轴ID
     * @param outAxis [输出参数] 成功则指向轴对象，失败为 nullptr
     * @param reason [输出参数] 失败时的具体拒绝原因
     * @return true 允许访问；false 拒绝访问
     *
     * 拦截优先级（从高到低）：
     *   Layer 0 -- 安全锁定：急停中 / 未同步 / 过渡中 -> SystemSafetyLocked
     *   Layer 1 -- 龙门同步：NotSynchronized -> GantryNotSynchronized
     *   Layer 2 -- 龙门语义：Coupled -> PhysicalAxisLockedByGantry / Decoupled -> LogicalAxisUnavailableWhenDecoupled
     *   Layer 3 -- 容器查找：AxisNotRegistered
     *   Layer 4 -- 通过：None
     *
     * 注意：此方法受 Layer 0 安全锁定拦截，用于控制操作（使能/运动）；
     *       纯遥测读取请使用 tryReadAxis()，它绕过安全锁定仅保留龙门语义。
     */
    bool tryGetAxis(AxisId id, Axis*& outAxis, ContextRejection& reason) {
        // ==========================================
        // Layer 0：安全域最高优先级拦截
        // 急停中 / 未同步 / 过渡中 -> 所有轴访问全部拒绝
        // ==========================================
        if (m_emergencyStopController.isSystemLocked()) {
            reason = ContextRejection::SystemSafetyLocked;
            outAxis = nullptr;
            // ★ 缺失项2：拒绝日志
            logAxisRejection(id, reason);
            return false;
        }

        return tryGetAxisInternal(id, outAxis, reason);
    }

    /**
     * @brief Try-Read 模式读取轴对象（遥测读取入口）
     *
     * 与 tryGetAxis() 的唯一区别：不经过 Layer 0 安全锁定拦截。
     * 急停/同步期间，位置与状态遥测仍然可读，但控制操作被拒绝。
     *
     * 拦截优先级：
     *   Layer 1 -- 龙门同步：NotSynchronized -> GantryNotSynchronized
     *   Layer 2 -- 龙门语义：Coupled -> PhysicalAxisLockedByGantry / Decoupled -> LogicalAxisUnavailableWhenDecoupled
     *   Layer 3 -- 容器查找：AxisNotRegistered
     *   Layer 4 -- 通过：None
     */
    bool tryReadAxis(AxisId id, Axis*& outAxis, ContextRejection& reason) {
        return tryGetAxisInternal(id, outAxis, reason);
    }

    // --- 龙门控制的基础接口 ---
    GantryCouplingController& gantryCouplingController() { return *m_gantryCouplingController; }
    GantryPowerController& gantryPowerController() { return *m_gantryPowerController; }

    // --- 安全急停接口 ---
    /**
     * @brief 急停控制器引用
     *
     * 暴露给外部用于：
     *   1. 查询 isSystemLocked() -- 在 UseCase 中快速判断
     *   2. 调用 requestEmergencyStop() / requestReleaseEmergencyStop() -- 产生急停/解除意图
     *   3. 调用 applyFeedback(plcEmergencyStopped) -- 注入 PLC 反馈，驱动状态机
     *   4. hasPendingCommand() / popPendingCommand() -- 从 SystemManager 消费命令
     */
    EmergencyStopController& emergencyStopController() { return m_emergencyStopController; }

    void setDriver(ISystemDriver* driver) { m_driver = driver; }
    ISystemDriver* driver() { return m_driver; }

    /**
     * @brief 直接为指定轴设置身份信息（绕过龙门语义拦截，仅用于日志系统初始化）
     * @param id 目标轴ID
     * @param groupName 所属分组名称（如 "Machine_A"）
     *
     * 注意：此方法不经过 tryGetAxisInternal 的龙门语义拦截，
     *       仅用于在系统初始化时为 Axis 实体注入身份信息。
     *       控制操作仍必须通过 tryGetAxis/tryReadAxis。
     */
    void setAxisIdentity(AxisId id, const std::string& groupName) {
        auto it = m_axes.find(id);
        if (it != m_axes.end()) {
            it->second->setIdentity(id, groupName);
        }
    }

private:
    /**
     * @brief 内部共用方法：龙门同步 + 龙门语义 + 容器查找
     *
     * 被 tryGetAxis() 和 tryReadAxis() 共用。
     * 不包含 Layer 0 安全锁定拦截（由 tryGetAxis 单独处理）。
     * 在被拒绝时输出详细日志（缺失项2）。
     */
    bool tryGetAxisInternal(AxisId id, Axis*& outAxis, ContextRejection& reason) {
        // 仅龙门相关轴受联动状态约束，非龙门轴跳过
        if (id == AxisId::X || id == AxisId::X1 || id == AxisId::X2) {
            // A. 前置拦截：状态机尚未同步，物理真相未知 -> 拒绝一切龙门轴访问
            if (m_gantryCouplingController->isNotSynchronized()) {
                reason = ContextRejection::GantryNotSynchronized;
                outAxis = nullptr;
                // ★ 缺失项2：拒绝日志（含龙门同步状态详情）
                logAxisRejection(id, reason, "couplingState=NotSynchronized");
                return false;
            }

            // B. 龙门联动/解耦语义：由 GantryCouplingController 唯一真相源裁决
            if (m_gantryCouplingController->isCoupled()) {
                if (id == AxisId::X1 || id == AxisId::X2) {
                    reason = ContextRejection::PhysicalAxisLockedByGantry;
                    outAxis = nullptr;
                    logAxisRejection(id, reason, "isCoupled=true");
                    return false;
                }
            } else {
                if (id == AxisId::X) {
                    reason = ContextRejection::LogicalAxisUnavailableWhenDecoupled;
                    outAxis = nullptr;
                    logAxisRejection(id, reason,
                        "couplingState="
                        + std::string(gantryCouplingStatusToString(m_gantryCouplingController->isCouplingRequested()
                            ? GantryCouplingState::Status::CouplingRequested
                            : m_gantryCouplingController->isDecouplingRequested()
                                ? GantryCouplingState::Status::DecouplingRequested
                                : GantryCouplingState::Status::Decoupled)));
                    return false;
                }
            }
        }

        // C. 容器查找
        auto it = m_axes.find(id);
        if (it == m_axes.end()) {
            reason = ContextRejection::AxisNotRegistered;
            outAxis = nullptr;
            logAxisRejection(id, reason);
            return false;
        }

        // D. 校验通过
        outAxis = it->second.get();
        reason = ContextRejection::None;
        return true;
    }

    /**
     * @brief 统一的轴访问拒绝日志（缺失项2）
     * @param id 被拒绝访问的轴ID
     * @param reason 拒绝原因
     * @param extraDetail 可选额外详情（如龙门同步状态）
     */
    void logAxisRejection(AxisId id, ContextRejection reason, const std::string& extraDetail = "") {
        std::ostringstream oss;
        oss << "tryGetAxis(" << axisIdToString(id) << ") REJECTED: "
            << contextRejectionToString(reason);
        if (!extraDetail.empty()) {
            oss << " (" << extraDetail << ")";
        }
        LOG_WARN_EVERY_MS(5000, LogLayer::DOM, "Context", oss.str());
    }

    std::unordered_map<AxisId, std::unique_ptr<Axis>> m_axes;
    std::unique_ptr<GantryCouplingController> m_gantryCouplingController;
    std::unique_ptr<GantryPowerController> m_gantryPowerController;
    EmergencyStopController m_emergencyStopController;  // 值语义，SystemContext 组合持有
    ISystemDriver* m_driver = nullptr;
};
