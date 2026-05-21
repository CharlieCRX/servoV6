#pragma once

/**
 * @brief 安全域操作拒绝原因
 *
 * 独立于 ContextRejection / GantryRejection / RejectionReason，
 * 因为安全域语义与其他域完全不同，保持领域边界清晰。
 */
enum class SafetyRejection {
    None,                       // 无拒绝（操作允许）

    // --- 状态机拒绝 ---
    SystemSafetyLocked,         // 系统处于 EmergencyStopping / EmergencyStopped / ReleasingEmergencyStop / NotSynchronized 状态
    AlreadyInState,             // 幂等保护：请求状态 = 当前状态（如已在急停中再次触发急停）
    InvalidStateTransition,     // 非法状态跃迁（如从 Running 直接请求 ReleasingEmergencyStop）
    NotSynchronized,            // 尚未同步 PLC 真实安全状态，拒绝所有请求

    // --- 前置条件拒绝 ---
    NotEmergencyStopped,        // 尝试解除急停但系统并未处于急停状态
};
