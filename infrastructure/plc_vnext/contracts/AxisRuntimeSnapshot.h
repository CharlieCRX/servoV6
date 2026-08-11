// ============================================================================
// AxisRuntimeSnapshot.h —— Step 7 contracts: 单槽位运行反馈快照
// ============================================================================
// 只表达 PLC 某个槽位（0..15）一次轮询得到的运行反馈，**不注入领域对象**。
//   - slot          ：PLC 数组下标（0..15），非 X/Y/Z/R 业务名。
//   - 各字段地址与《PLC变量协议_Modbus最终地址表.md》§3.1 及
//     tools/plc_read_validate.py STANDARD_AXIS_BLOCKS 完全一致：
//       REAL 每项占 2 D（低字在前 CDAB）；INT/WORD 每项占 1 D；0 基址。
//   - trusted=false ：本次读取缺数据/失败，字段默认值**不得**被当作正常值使用；
//                     由上层按 trusted 决定是否锁定普通控制。
// 纯 DTO：不依赖 Modbus / Qt / Domain；不出现业务轴名。
// ============================================================================
#pragma once

#include <cstdint>

namespace plc_vnext::contracts {

/// 单个 PLC 槽位的运行反馈快照。
struct AxisRuntimeSnapshot {
    int16_t slot = 0;             ///< PLC 数组下标 0..15

    float manualSpeed = 0.0f;     ///< D(0+2i)..D(1+2i)     REAL，EU/s
    float positioningSpeed = 0.0f;///< D(32+2i)..D(33+2i)   REAL，EU/s
    float absPosition = 0.0f;     ///< D(64+2i)..D(65+2i)   REAL，EU
    float relPosition = 0.0f;     ///< D(96+2i)..D(97+2i)   REAL，EU

    int16_t motionState = 0;      ///< D(128+i)  INT，原样保留（不校验枚举）
    int16_t motionLimit = 0;      ///< D(144+i)  INT，原样保留（不校验枚举）
    uint16_t alarmWord = 0;       ///< D(160+i)  WORD，位掩码

    /// 本次读取是否可信。缺数据/读取失败时为 false，字段默认值不冒充正常。
    bool trusted = false;
};

}  // namespace plc_vnext::contracts
