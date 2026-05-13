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
};
