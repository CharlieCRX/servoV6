// =============================================================================
// AxisStateDeriver — 多信号融合状态推导引擎 (header-only)
//
// 职责：将 PLC 多个寄存器/Coil 的离散信号融合为 Domain 统一的 AxisState
//
// 设计原则：
//   1. 纯函数 — 无状态、无副作用、无依赖注入，仅依赖输入参数
//   2. 确定性 — 相同输入永远产生相同输出
//   3. 优先链 — 严格按 Error → Disabled → MovingAbsolute → MovingRelative
//                → Jogging → Idle 优先级判断
//
// 设计依据：
//   - 《SystemCommand寄存器映射与Domain-Infrastructure对接设计》§4.8.3
//   - 《阶段三-状态推导引擎TDD详细开发文档》§3
// =============================================================================

#pragma once

#include "domain/entity/Axis.h"
#include <cstdint>

namespace plc {

/**
 * @brief 多信号融合：将 PLC 离散信号推导为 Domain 统一 AxisState
 *
 * 输入参数来自 PLC 不同寄存器：
 *   - d100State  : D100~D103 (STATE) — Int16
 *                  0=未使能, 1=使能(静止), 2=使能(运动), 3=报警
 *   - alarmCode  : D110~D113 (ALARM_CODE) — Int16
 *                  0=正常, 非零=报警码
 *   - absMoving  : M110/M113/M116/M119 (ABS_MOVING) — Bool
 *   - relMoving  : M111/M114/M117/M120 (REL_MOVING) — Bool
 *   - jogging    : M112/M115/M118/M121 (JOGGING) — Bool
 *
 * 推导优先级链：
 *   1. d100State == 3 || alarmCode != 0  → AxisState::Error
 *   2. d100State == 0                    → AxisState::Disabled
 *   3. absMoving                         → AxisState::MovingAbsolute
 *   4. relMoving                         → AxisState::MovingRelative
 *   5. jogging                           → AxisState::Jogging
 *   6. else                              → AxisState::Idle
 *
 * @note 此函数为纯函数，不依赖任何外部状态。
 *       PLC 连接正常时，d100State 一定在 {0,1,2,3} 之内。
 *
 * @return 推导出的 AxisState，永远不会返回 AxisState::Unknown
 */
inline AxisState deriveAxisState(int16_t d100State,
                                 int16_t alarmCode,
                                 bool absMoving,
                                 bool relMoving,
                                 bool jogging) {
    // 优先级 1：报警（最高优先级）
    //   d100State == 3 是 PLC 标准报警状态码
    //   alarmCode != 0 作为冗余兜底，两个条件 OR 关系
    if (d100State == 3 || alarmCode != 0) {
        return AxisState::Error;
    }

    // 优先级 2：未使能
    if (d100State == 0) {
        return AxisState::Disabled;
    }

    // 以下 d100State ∈ {1, 2}：使能状态
    // 由独立的 Coil 信号决定精确子状态
    // 多圈编码器抖跳导致的 D100=1↔2 变化被此 if-else 链消除
    if (absMoving) {
        return AxisState::MovingAbsolute;
    }
    if (relMoving) {
        return AxisState::MovingRelative;
    }
    if (jogging) {
        return AxisState::Jogging;
    }

    // 使能但无任何运动信号
    return AxisState::Idle;
}

} // namespace plc
