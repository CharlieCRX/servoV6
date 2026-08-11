// ============================================================================
// PlcAxisCommandWriter.h —— Step 8 command: 单轴命令写入器
// ============================================================================
// 把"槽位参数/目标/使能/点动/触发/清除"编码为 Modbus 写；只报告写入是否到达
// PLC（contracts::CommunicationResult），不决定是否运动、不伪造 PLC 执行成功。
// 目标与触发永远是两次独立调用（见设计文档 §4.3）。
//
// ★ 真机写入保持关闭：本 writer 仅通过 transport::IModbusClient 窄接口工作，
//   测试一律注入 FakeModbusClient；未接入 AsioModbusTcpClient 的写路径，
//   真实 PLC 写验收属于受控上线活动（Step 8/9/11 完成条件），不在本离线
//   TDD 内开放，也不会对运行设备产生任何写操作。
// ============================================================================
#pragma once

#include <memory>

#include "infrastructure/plc_vnext/command/CommandWritePolicy.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"
#include "infrastructure/plc_vnext/layout/RegisterAddress.h"
#include "infrastructure/plc_vnext/transport/IModbusClient.h"

namespace plc_vnext::command {

/// 单轴命令写入器。构造注入 transport client（测试用 FakeModbusClient）。
class PlcAxisCommandWriter {
public:
    explicit PlcAxisCommandWriter(transport::IModbusClientPtr client);

    contracts::CommunicationResult write(contracts::PlcAxisSlot slot,
                                         const contracts::PlcAxisCommand& cmd);

private:
    // 保持寄存器参数写（REAL，低字在前 CDAB）
    contracts::CommunicationResult writeParameter(layout::HoldingAddress base,
                                                  float value);
    // 保持电平线圈写
    contracts::CommunicationResult writeLevelCoil(layout::CoilAddress addr, bool value);
    // PLC 自复位写（只写 ON；触发/终止/清除由 PLC 自动复位，无需配对 OFF）
    contracts::CommunicationResult writeSelfReset(layout::CoilAddress addr);

    transport::IModbusClientPtr m_client;
};

}  // namespace plc_vnext::command
