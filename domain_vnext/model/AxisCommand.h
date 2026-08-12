// ============================================================================
// AxisCommand.h —— P1 model: 14 项触发 + 参数写命令
// ============================================================================
// 纯领域命令枚举/DTO。最终由 CommandMapper（P4）映射到
// plc_vnext::contracts::PlcAxisCommand。
//   - 14 项触发对应 M 区线圈；参数写对应 D 区保持寄存器。
//   - 解耦铁律：SetAbsDistance 与 TriggerAbsMove、SetRelDistance 与
//     TriggerRelMove 是四个独立接口，领域层绝不合并为「设置目标 + 触发」。
// 纯 DTO：只依赖自身 + plc_vnext::contracts。
// ============================================================================
#pragma once

#include <cstdint>

#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"

namespace domain_vnext::model {

/// 单轴领域命令类别。
enum class AxisCommandKind {
    // ---- 14 项触发（映射到 plc_vnext::PlcAxisCommand）----
    EnableAxis,        //  1 M0    使能轴控
    ClearRelZero,      //  2 M16   相对原点清除
    ClearAbsPosition,  //  3 M32   绝对位置清零
    TriggerAbsMove,    //  4 M48   绝对定位触发
    TriggerRelMove,    //  5 M64   相对定位触发
    JogForward,        //  6 M80   点动正转
    JogBackward,       //  7 M96   点动反转
    ResetAlarm,        //  8 M112  报警解除触发（PLC 能力位判定）
    EnableMotor,       //  9 M128  使能电机
    StopRelMove,       // 10 M144  相对定位终止
    StopAbsMove,       // 11 M160  绝对定位终止
    SetRelZero,        // 12 M176  相对原点设置
    JogHeartbeat,      // 13 M192  点动心跳
    ClearAlarmWord,    // 14 M208  告警码置零

    // ---- 参数写（RW 保持寄存器）----
    SetManualSpeed,        // D0
    SetPositioningSpeed,   // D32
    SetAbsDistance,        // D1096
    SetRelDistance,        // D1128
    SetSoftNegLimit,       // D1160
    SetSoftPosLimit,       // D1192
    SetSoftLimitControl,   // D1228
    SetRelZeroRecord,      // D1064
};

/// 领域命令值对象。
struct AxisCommand {
    AxisCommandKind kind = AxisCommandKind::EnableAxis;
    float value = 0.f;   ///< 参数写目标（REAL）
    bool level = false;  ///< 保持电平线圈（Enable*/Jog*）
};

/// 携带目标槽位的命令信封。
struct AxisCommandEnvelope {
    plc_vnext::contracts::PlcAxisSlot slot;
    AxisCommand cmd;
};

}  // namespace domain_vnext::model
