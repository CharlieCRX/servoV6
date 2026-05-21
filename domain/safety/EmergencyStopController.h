#pragma once

#include "safety/SafetyState.h"
#include "safety/SafetyRejection.h"
#include "command/SystemCommand.h"  // EmergencyStopCommand
#include <optional>

/**
 * @brief 急停控制器 -- 五态工业安全状态机（含 Startup Synchronization）
 *
 * 设计原则：
 *   1. 单一反馈入口 -- applyFeedback() 是 PLC Feedback 的唯一入口，包括首次同步
 *   2. 启动同步 -- 初始态 NotSynchronized，首次 applyFeedback() 完成同步
 *   3. 反馈驱动 -- 状态变更由 PLC 反馈确认，不由请求立即变更
 *   4. 不持有 Axis 引用 -- 轴的访问拒绝由 SystemContext::tryGetAxis() 实现
 *   5. 幂等安全 -- 重复触发/解除急停不产生副作用
 *   6. 命令与状态分离 -- PLC 有独立的"急停命令"寄存器和"急停中"状态寄存器
 *   7. 安全域不管理轴生命周期 -- 急停解除后直接回到 Running，轴使能由 Axis 域管理
 *   8. Controller 永远相信 PLC Feedback -- 不与物理真相为敌
 */
class EmergencyStopController {
public:
    EmergencyStopController() = default;

    // ==========================================
    // 状态查询
    // ==========================================

    SafetyState state() const { return m_state; }

    /// @brief 系统是否处于"锁定"状态（任何运动都不允许）
    /// @return true 当状态为 NotSynchronized / EmergencyStopping / EmergencyStopped / ReleasingEmergencyStop
    bool isSystemLocked() const {
        return m_state == SafetyState::NotSynchronized ||
               m_state == SafetyState::EmergencyStopping ||
               m_state == SafetyState::EmergencyStopped ||
               m_state == SafetyState::ReleasingEmergencyStop;
    }

    /// @brief 系统是否处于急停锁定态（PLC 已确认停机）
    bool isEmergencyStopped() const {
        return m_state == SafetyState::EmergencyStopped;
    }

    /// @brief 系统是否正在过渡中（等待 PLC 反馈）
    bool isTransitioning() const {
        return m_state == SafetyState::EmergencyStopping ||
               m_state == SafetyState::ReleasingEmergencyStop;
    }

    /// @brief 系统是否尚未收到任何 PLC 反馈（尚未同步）
    bool isNotSynchronized() const {
        return m_state == SafetyState::NotSynchronized;
    }

    // ==========================================
    // 意图生成（Produce Intent）
    // ==========================================

    /**
     * @brief 请求触发急停
     *
     * 合法来源状态：Running
     * 产生意图：EmergencyStopCommand{ true }
     * 本地状态 -> EmergencyStopping
     *
     * @return None 表示命令已生成，等待 PLC 反馈
     * @return NotSynchronized 表示尚未同步 PLC 状态，拒绝操作
     * @return AlreadyInState 表示已在急停流程中（幂等）
     * @return InvalidStateTransition 表示当前状态不允许此操作
     */
    SafetyRejection requestEmergencyStop() {
        // 尚未同步 PLC 状态，拒绝所有操作
        if (m_state == SafetyState::NotSynchronized) {
            return SafetyRejection::NotSynchronized;
        }
        // 幂等：已在急停流程中
        if (m_state == SafetyState::EmergencyStopping ||
            m_state == SafetyState::EmergencyStopped) {
            return SafetyRejection::AlreadyInState;
        }
        // 冲突：正在解除急停，不允许反向操作
        if (m_state == SafetyState::ReleasingEmergencyStop) {
            return SafetyRejection::InvalidStateTransition;
        }
        // 通过：Running -> 生成急停意图
        m_pending_intent = EmergencyStopCommand{ true };
        m_state = SafetyState::EmergencyStopping;
        return SafetyRejection::None;
    }

    /**
     * @brief 请求解除急停
     *
     * 合法来源状态：EmergencyStopped
     * 产生意图：EmergencyStopCommand{ false }
     * 本地状态 -> ReleasingEmergencyStop
     *
     * @return None 表示命令已生成，等待 PLC 反馈
     * @return NotSynchronized 表示尚未同步 PLC 状态，拒绝操作
     * @return NotEmergencyStopped 表示当前不处于急停锁定状态
     */
    SafetyRejection requestReleaseEmergencyStop() {
        // 尚未同步 PLC 状态，拒绝所有操作
        if (m_state == SafetyState::NotSynchronized) {
            return SafetyRejection::NotSynchronized;
        }
        // 前置条件：只有 EmergencyStopped 状态才能解除
        if (m_state != SafetyState::EmergencyStopped) {
            return SafetyRejection::NotEmergencyStopped;
        }
        m_pending_intent = EmergencyStopCommand{ false };
        m_state = SafetyState::ReleasingEmergencyStop;
        return SafetyRejection::None;
    }

    // ==========================================
    // 反馈驱动（Apply Feedback）-- 唯一的 PLC Feedback 入口
    // ==========================================

    /**
     * @brief 接收 PLC 的"急停中"状态反馈，驱动本地状态机
     *
     * 这是所有 PLC Feedback 的单一入口，包括：
     *   - 首次同步（NotSynchronized -> Running / EmergencyStopped）
     *   - 正常运行期状态确认
     *
     * @param plcEmergencyStopped 对应 PLC 寄存器"设备急停中"
     *
     * 状态跃迁规则：
     *   NotSynchronized         + plcEmergencyStopped == false -> Running（首次同步）
     *   NotSynchronized         + plcEmergencyStopped == true  -> EmergencyStopped（首次同步）
     *   EmergencyStopping       + plcEmergencyStopped == true  -> EmergencyStopped
     *   EmergencyStopped        -> 保持（PLC 反馈 false 时也保持，不信任意外波动）
     *   ReleasingEmergencyStop  + plcEmergencyStopped == false -> Running
     *   Running                 + plcEmergencyStopped == true  -> EmergencyStopped（物理急停按钮）
     */
    void applyFeedback(bool plcEmergencyStopped) {
        switch (m_state) {
        case SafetyState::NotSynchronized:
            // 首次同步：PLC Feedback 是唯一的真相来源
            if (plcEmergencyStopped) {
                m_state = SafetyState::EmergencyStopped;
            } else {
                m_state = SafetyState::Running;
            }
            break;

        case SafetyState::EmergencyStopping:
            // 等待 PLC 确认急停完成
            if (plcEmergencyStopped) {
                m_state = SafetyState::EmergencyStopped;
            }
            // 如果 PLC 尚未反馈 true，保持 EmergencyStopping 继续等待
            break;

        case SafetyState::EmergencyStopped:
            // EmergencyStopped 是安全锁存态（Latched State）。
            // 一旦进入该状态，不会因为 PLC feedback 恢复而自动退出。
            // 必须经过显式的 requestReleaseEmergencyStop() 流程。
            break;

        case SafetyState::ReleasingEmergencyStop:
            // 等待 PLC 确认急停解除 -> 直接恢复 Running
            // 不经过任何中间状态，因为 servoV6 默认 Disabled，
            // 轴的使能生命周期由 Axis 域独立管理
            if (!plcEmergencyStopped) {
                m_state = SafetyState::Running;
            }
            // 如果 PLC 尚未反馈 false，保持 ReleasingEmergencyStop 继续等待
            break;

        case SafetyState::Running:
            // 物理急停按钮被按下，PLC 直接反馈 true
            // Controller 永远相信 PLC Feedback，直接从 Running 跃迁到 EmergencyStopped
            // 不经过 EmergencyStopping（因为命令不由 Controller 发出）
            if (plcEmergencyStopped) {
                m_state = SafetyState::EmergencyStopped;
            }
            break;
        }
    }

    // ==========================================
    // 命令消费（Pop Intent）
    // ==========================================

    bool hasPendingCommand() const {
        return m_pending_intent.has_value();
    }

    EmergencyStopCommand popPendingCommand() {
        auto cmd = *m_pending_intent;
        m_pending_intent.reset();
        return cmd;
    }

private:
    SafetyState m_state = SafetyState::NotSynchronized;
    std::optional<EmergencyStopCommand> m_pending_intent;
};
