// ============================================================================
// AxisState.h —— P1 model: 运动状态 / 运动限制 / 软限位控制
// ============================================================================
// 依据《PLC变量协议_Modbus地址表》§3 直接解码 D128（运动状态）/ D144（运动限制）
// / D1228（软限位控制字），不再由旧 Coil 组合推导。
// 纯领域值对象：只依赖自身 + plc_vnext::contracts。
// ============================================================================
#pragma once

#include <cstdint>

namespace domain_vnext::model {

/// 运动状态编码（直接解码 D128）。
enum class MotionState : std::uint16_t {
    ControlNotEnabled = 0,  // 轴控入口未使能
    Idle = 1,
    JogForward = 2,
    JogBackward = 3,
    MovingAbsolute = 4,
    MovingRelative = 5,
};

/// 运动限制编码（直接解码 D144）。
enum class LimitState : std::uint16_t {
    None = 0,
    PositiveSoftware = 1,
    NegativeSoftware = 2,
    PositiveHardware = 3,
    NegativeHardware = 4,
};

/// 软限位控制字（D1228）：bit0 正限位使能、bit1 负限位使能。
struct SoftLimitControl {
    bool positiveEnabled = false;
    bool negativeEnabled = false;

    /// 从 PLC 原始 WORD（D1228）解码。
    static SoftLimitControl decode(std::uint16_t raw) {
        return {(raw & 0x01u) != 0u, (raw & 0x02u) != 0u};
    }
    /// 编码回 PLC 原始 WORD。
    [[nodiscard]] std::uint16_t encode() const {
        return static_cast<std::uint16_t>(
            (positiveEnabled ? 0x01u : 0u) | (negativeEnabled ? 0x02u : 0u));
    }
};

}  // namespace domain_vnext::model
