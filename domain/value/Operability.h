#pragma once

#include <cstdint>

/**
 * @file Operability.h
 * @brief 操作可行性枚举 (GantryValue namespace)
 *
 * 用于描述 GantrySystem 中运动命令的可行性检查结果。
 * 只有 Allowed 表示操作可以通过，其余值表示具体的拒绝原因。
 *
 * 约束映射：
 *   约束15 — 限位触发后拒绝所有 Move
 *   约束16 — 限位触发后禁止向限位方向 Jog
 *   约束17 — 报警状态下禁止运动
 *   约束18 — 操作目标互斥（Coupled 只可操作 X，Decoupled 只可操作 X1/X2）
 *   约束19 — 命令槽互斥
 *
 * 注：此枚举与 GantrySystem.h 中的旧 Operability 并行存在，
 *     以 GantryValue 命名空间隔离，避免 ODR 冲突。
 *     后续 TDD 迭代中将用此版本统一替换旧版本。
 */
namespace GantryValue {

enum class Operability : uint8_t {
    Allowed                    = 0,  ///< 操作允许
    TargetNotOperableInMode    = 1,  ///< 当前模式下目标不可操作（约束18）
    AlarmActive                = 2,  ///< 报警激活（约束17）
    LimitTriggered             = 3,  ///< 限位触发（约束15）
    LimitBlocksDirection       = 4,  ///< 限位禁止该方向运动（约束16）
    NotEnabled                 = 5,  ///< 轴未使能
    CommandSlotBusy            = 6,  ///< 命令槽忙（约束19）
    NotIdle                    = 7,  ///< 轴非空闲
    DeviationExceeded          = 8,  ///< 同步偏差超限
};

} // namespace GantryValue
