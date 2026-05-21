#pragma once
enum class ContextRejection {
    None,

    // --- SystemContext 内部（已有）---
    PhysicalAxisLockedByGantry,
    LogicalAxisUnavailableWhenDecoupled,
    GantryNotSynchronized,     // 龙门状态机尚未与 PLC 物理状态同步，拒绝 X/X1/X2 访问
    AxisNotRegistered,

    // --- SystemManager 分组管理（新增）---
    GroupAlreadyExists,       // 创建分组时，同名分组已存在
    GroupNotFound,            // 查找分组时，指定名称不存在
    GroupNameInvalid,         // 分组名称为空或包含非法字符

    DriverNotReady,           // 驱动未就绪（已有）

    // --- 安全域锁定（新增）---
    SystemSafetyLocked,       // 系统处于安全锁定状态（急停中 / 急停解除中），禁止轴访问
};

/// @brief 将 ContextRejection 枚举值转换为可读字符串（用于日志输出）
inline const char* contextRejectionToString(ContextRejection r) {
    switch (r) {
        case ContextRejection::None:                               return "None";
        case ContextRejection::PhysicalAxisLockedByGantry:          return "PhysicalAxisLockedByGantry";
        case ContextRejection::LogicalAxisUnavailableWhenDecoupled: return "LogicalAxisUnavailableWhenDecoupled";
        case ContextRejection::GantryNotSynchronized:               return "GantryNotSynchronized";
        case ContextRejection::AxisNotRegistered:                   return "AxisNotRegistered";
        case ContextRejection::GroupAlreadyExists:                  return "GroupAlreadyExists";
        case ContextRejection::GroupNotFound:                       return "GroupNotFound";
        case ContextRejection::GroupNameInvalid:                    return "GroupNameInvalid";
        case ContextRejection::DriverNotReady:                      return "DriverNotReady";
        case ContextRejection::SystemSafetyLocked:                  return "SystemSafetyLocked";
    }
    return "?";
}
