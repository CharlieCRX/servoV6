#pragma once
#include <variant>
#include "entity/ContextRejection.h"
#include "entity/Axis.h"              // RejectionReason
#include "gantry/GantryRejection.h"
#include "safety/SafetyRejection.h"
#include "infrastructure/ISystemDriver.h"  // CommunicationResult

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
    SafetyRejection         // 安全域急停层
>;
