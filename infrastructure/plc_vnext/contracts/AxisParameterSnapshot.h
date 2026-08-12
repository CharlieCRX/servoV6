// ============================================================================
// AxisParameterSnapshot.h —— contracts: 单槽位参数区（RW）反馈快照
// ============================================================================
// 依据《Domain层重构设计——domain_vnext》§7.2：前 7 项运行反馈由
// AxisRuntimeSnapshot 提供，参数区 8~13（RW 保持寄存器）由本快照补充。
// 字段地址与《PLC变量协议_Modbus最终地址表.md》§3 一致（REAL 每项占 2 D，
// 低字在前 CDAB；WORD 占 1 D；0 基址）：
//   8  相对原点记录  relZeroRecord   D(1064+2s)
//   9  绝对定位距离  absMoveDistance D(1096+2s)
//   10 相对定位距离  relMoveDistance D(1128+2s)
//   11 软件负限位    softNegLimit    D(1160+2s)
//   12 软件正限位    softPosLimit    D(1192+2s)
//   13 软限位控制    softLimitControl D(1228+s) bit0正 bit1负
// trusted=false 表示本次读取缺数据/失败，字段默认值不得被当作正常值。
// 纯 DTO：不依赖 Modbus / Qt / Domain。
// ============================================================================
#pragma once

#include <cstdint>

namespace plc_vnext::contracts {

/// 单个 PLC 槽位的参数区（RW）反馈快照。
struct AxisParameterSnapshot {
    int16_t slot = 0;              ///< PLC 数组下标 0..15

    float relZeroRecord = 0.0f;    ///< D(1064+2s)..D(1065+2s)  REAL
    float absMoveDistance = 0.0f;  ///< D(1096+2s)..D(1097+2s)  REAL
    float relMoveDistance = 0.0f;  ///< D(1128+2s)..D(1129+2s)  REAL
    float softNegLimit = 0.0f;     ///< D(1160+2s)..D(1161+2s)  REAL
    float softPosLimit = 0.0f;     ///< D(1192+2s)..D(1193+2s)  REAL
    uint16_t softLimitControl = 0; ///< D(1228+s) WORD，bit0正 bit1负

    /// 本次读取是否可信。缺数据/读取失败时为 false，字段默认值不冒充正常。
    bool trusted = false;
};

}  // namespace plc_vnext::contracts
