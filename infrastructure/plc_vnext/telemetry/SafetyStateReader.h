// ============================================================================
// SafetyStateReader.h —— 阶段 2 telemetry: 急停只读器（M224/M225）
// ============================================================================
// 只读设备急停线圈反馈（方案 §4.4）：
//   - M224 = 设备急停（Emergency Stop）
//   - M225 = 设备急停解除请求
// 经 transport::IModbusClient 以 FC01 读 0 基址 224 起 2 个线圈，解码 bit0=M224、
// bit1=M225。调用方把**同一个 IModbusClient**（即 PlcRuntimeGateway 内部共享的
// ModbusIoExecutor 串行通道）注入，保证急停读取与拓扑/运行读取共用同一串行
// I/O 通道。
// 读取失败不以“正常”冒充：trusted=false + diagnostic 保留；本类不创建 Domain
// 状态机、不做急停语义判定、不写 PLC。
// ============================================================================
#pragma once

#include <memory>

#include "infrastructure/plc_vnext/contracts/SafetySnapshot.h"
#include "infrastructure/plc_vnext/transport/IModbusClient.h"

namespace plc_vnext::telemetry {

class SafetyStateReader {
public:
    /// @param client 设备 I/O 客户端（应为共享串行化通道 ModbusIoExecutor）
    explicit SafetyStateReader(transport::IModbusClientPtr client);

    /// 读取 M224/M225，产出急停快照；失败以 trusted=false 表达，不抛异常。
    [[nodiscard]] contracts::SafetySnapshot read();

private:
    transport::IModbusClientPtr m_client;
};

}  // namespace plc_vnext::telemetry
