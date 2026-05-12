#pragma once
enum class ContextRejection {
    None,

    // --- SystemContext 内部（已有）---
    PhysicalAxisLockedByGantry,
    LogicalAxisUnavailableWhenDecoupled,
    AxisNotRegistered,

    // --- SystemManager 分组管理（新增）---
    GroupAlreadyExists,       // 创建分组时，同名分组已存在
    GroupNotFound,            // 查找分组时，指定名称不存在
    GroupNameInvalid,         // 分组名称为空或包含非法字符

    DriverNotReady,           // 驱动未就绪（已有）
};