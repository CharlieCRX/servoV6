// ============================================================================
// GantryStatusSnapshot.h —— Step 7 contracts: 龙门状态快照
// ============================================================================
// 只表达 PLC GantryStatus[g]（基址 D190 + 18*g，g=0..1）一次轮询的输出，
// **不注入领域对象**。字段偏移与《PLC变量协议_Modbus最终地址表.md》§8 及
// tools/plc_read_validate.py GANTRY_STATUS_FIELDS 完全一致：
//   State/InternalStep +0/+1；AckSeq(DINT) +2..+3；CommandResult +4；
//   CommandErrorCode +5；+6.bit0..5 控制/啮合许可；X1/X2/Logical/Skew REAL +7..+14；
//   Fault +15.bit0；FaultCode +16；Reserved +17。端序 CDAB（低字在前）。
// trusted=false 表示本次读取缺数据/失败，字段默认值不得被当作正常值。
// 纯 DTO：不依赖 Modbus / Qt / Domain；不出现业务轴名。
// ============================================================================
#pragma once

#include <cstdint>

namespace plc_vnext::contracts {

/// 单个龙门组的运行状态快照。
struct GantryStatusSnapshot {
    int16_t state = 0;             ///< +0 INT：0未配置 1已解除 2建立中 3已联动 4解除中 5故障
    int16_t internalStep = 0;      ///< +1 INT：内部步骤（仅诊断）
    int32_t ackSeq = 0;            ///< +2..+3 DINT：确认序号（低字在前）
    int16_t commandResult = 0;     ///< +4 INT：0无结果 1处理中 2成功 3拒绝 4失败
    int16_t commandErrorCode = 0;  ///< +5 INT
    bool readyToCouple = false;    ///< +6.bit0
    bool readyToDecouple = false;  ///< +6.bit1
    bool memberControlAllowed = false;    ///< +6.bit2
    bool logicalControlAllowed = false;   ///< +6.bit3
    bool x1InGear = false;         ///< +6.bit4
    bool x2InGear = false;         ///< +6.bit5

    float x1Position = 0.0f;       ///< +7..+8 REAL，mm
    float x2Position = 0.0f;       ///< +9..+10 REAL，mm
    float logicalPosition = 0.0f;  ///< +11..+12 REAL，mm
    float skew = 0.0f;             ///< +13..+14 REAL，X1-X2，mm

    bool fault = false;            ///< +15.bit0
    int16_t faultCode = 0;         ///< +16 INT
    int16_t reserved = 0;          ///< +17 INT

    /// 本次读取是否可信。缺数据/读取失败时为 false，字段默认值不冒充正常。
    bool trusted = false;
};

}  // namespace plc_vnext::contracts
