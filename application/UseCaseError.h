#pragma once
#include <variant>
#include "entity/ContextRejection.h"
#include "entity/Axis.h"              // RejectionReason
#include "gantry/GantryRejection.h"
#include "safety/SafetyRejection.h"
#include "infrastructure/ISystemDriver.h"  // CommunicationResult
#include <string>                            // std::string

/**
 * @brief Policy 策略层超时错误
 *
 * 与 CommunicationResult 语义不同：
 *   - CommunicationResult：命令已生成但未送达 PLC（传输级失败）
 *   - ErrTimeout：命令已成功送达 PLC，但预期反馈在时限内未返回（协议级超时）
 *
 * 用于 Policy 层的等待超时场景，例如：
 *   - EnsuringEnabled：发送使能后 2s 内未收到 Idle feedback → ErrTimeout
 */
struct ErrTimeout {
    std::string step;    // 超时发生的步骤名称（如 "EnsuringEnabled"）
    double timeoutSec;   // 超时阈值（秒）
};

/**
 * @brief 应用层统一的错误聚合类型
 * 
 * 各领域层保持自己的错误枚举独立定义（Axis::RejectionReason、
 * ContextRejection、GantryRejection、SafetyRejection），UseCase 在跨越多个层级时，
 * 用此 variant 聚合所有可能的错误类型，不做类型擦除。
 * 
 * std::monostate 代表"执行成功，无错误"。
 */
using UseCaseError = std::variant<
    std::monostate,         // 成功（领域规则通过 + 通讯送达）
    ContextRejection,       // SystemManager / SystemContext 层
    RejectionReason,        // Axis 领域层（命令未生成）
    CommunicationResult,    // 通讯失败（命令已生成但未送达 PLC）
    GantryRejection,        // Gantry 联动层
    SafetyRejection,        // 安全域急停层
    ErrTimeout              // ★ 策略层超时（命令已送达但反馈延迟）
>;
