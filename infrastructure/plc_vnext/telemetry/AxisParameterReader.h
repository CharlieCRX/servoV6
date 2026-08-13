// ============================================================================
// AxisParameterReader.h —— 阶段3 telemetry: 单槽位参数区（RW）读取器
// ============================================================================
// 读取某个槽位（0..15）的参数区（RW 保持寄存器）并解码为
// contracts::AxisParameterSnapshot，供阶段 3“参数写入/恢复读回确认”使用：
//   8  相对原点记录  relZeroRecord    D(1064+2s)  REAL
//   9  绝对定位距离  absMoveDistance  D(1096+2s)  REAL
//   10 相对定位距离  relMoveDistance  D(1128+2s)  REAL
//   11 软件负限位    softNegLimit     D(1160+2s)  REAL
//   12 软件正限位    softPosLimit     D(1192+2s)  REAL
//   13 软限位控制    softLimitControl D(1228+s)   WORD
// 经 transport::IModbusClient 以 FC03 读取（0 基址，低字在前 CDAB）。
// 任一字段读取失败 → trusted=false + 诊断保留，不得把字段置 0 冒充“正常”。
// 本类只读，不创建 Domain 轴、不做语义校验、不写 PLC。
// ============================================================================
#pragma once

#include <memory>
#include <string>

#include "infrastructure/plc_vnext/contracts/AxisParameterSnapshot.h"
#include "infrastructure/plc_vnext/transport/IModbusClient.h"

namespace plc_vnext::telemetry {

class AxisParameterReader {
public:
    /// @param client 设备 I/O 客户端（应为共享串行化通道 ModbusIoExecutor）
    explicit AxisParameterReader(transport::IModbusClientPtr client);

    /// 读取指定槽位的参数区快照；失败以 trusted=false 表达，不抛异常。
    [[nodiscard]] contracts::AxisParameterSnapshot read(int slot);

private:
    transport::IModbusClientPtr m_client;
};

}  // namespace plc_vnext::telemetry
