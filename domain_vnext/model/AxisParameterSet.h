// ============================================================================
// AxisParameterSet.h —— P1 model: 单轴 13 项设置快照
// ============================================================================
// 纯领域 DTO。前 7 项由 plc_vnext::contracts::AxisRuntimeSnapshot 提供（反馈侧，
// 只读注入）；后 6 项（8~13）是 RW 参数区，当前不在 RuntimeSnapshot 里，需要
// plc_vnext 补 AxisParameterSnapshot（见设计稿 §7）。trusted 表示本次读取是否可信。
// 纯 DTO：只依赖自身 + plc_vnext::contracts。
// ============================================================================
#pragma once

#include <cstdint>

#include "domain_vnext/model/AxisState.h"

namespace domain_vnext::model {

/// 单轴 13 项设置快照（读写）。
struct AxisParameterSet {
    // ---- 反馈侧（来自 RuntimeSnapshot，只读注入）----
    float manualSpeed       = 0.f;   // 1 手动速度   D(0+2s)
    float positioningSpeed  = 0.f;   // 2 定位速度   D(32+2s)
    float absPosition       = 0.f;   // 3 绝对位置   D(64+2s)
    float relPosition       = 0.f;   // 4 相对位置   D(96+2s)
    int16_t motionState     = 0;     // 5 运动状态   D(128+s)
    int16_t motionLimit     = 0;     // 6 运动限制   D(144+s)
    uint16_t alarmWord      = 0;     // 7 告警码     D(160+s)

    // ---- 参数区（RW，需 plc_vnext 补 AxisParameterSnapshot）----
    float relZeroRecord     = 0.f;   // 8  相对原点记录  D(1064+2s)
    float absMoveDistance   = 0.f;   // 9  绝对定位距离  D(1096+2s)
    float relMoveDistance   = 0.f;   // 10 相对定位距离  D(1128+2s)
    float softNegLimit      = 0.f;   // 11 软件负限位    D(1160+2s)
    float softPosLimit      = 0.f;   // 12 软件正限位    D(1192+2s)
    uint16_t softLimitControl = 0;   // 13 软限位控制    D(1228+s) bit0正 bit1负

    /// 本次读取是否可信。缺数据/读取失败时为 false，字段默认值不冒充正常。
    bool trusted = false;

    /// 运动状态编码 -> 领域枚举（直接解码 D128）。
    [[nodiscard]] MotionState motionStateEnum() const {
        return static_cast<MotionState>(static_cast<std::uint16_t>(motionState));
    }
    /// 运动限制编码 -> 领域枚举（直接解码 D144）。
    [[nodiscard]] LimitState limitStateEnum() const {
        return static_cast<LimitState>(static_cast<std::uint16_t>(motionLimit));
    }
    /// 软限位控制字 -> 领域值对象（解码 D1228）。
    [[nodiscard]] SoftLimitControl softLimitControlValue() const {
        return SoftLimitControl::decode(softLimitControl);
    }
};

}  // namespace domain_vnext::model
