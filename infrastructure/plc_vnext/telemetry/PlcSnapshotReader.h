// ============================================================================
// PlcSnapshotReader.h —— Step 7 telemetry: 执行 ReadPlan 生成 RuntimeSnapshot
// ============================================================================
// 只读读取器：把标准 16 槽位 D 区（D0..D175）与龙门状态区（D190..D225）纳入
// Step 4 的 ReadPlanBuilder，经 transport::IModbusClient 读取，再用
// AxisSnapshotDecoder × 16 与 GantryStatusReader × 2 解码为 RuntimeSnapshot。
//
// 读取失败不以异常或 nullopt 抛出，而是体现在 RuntimeSnapshot.quality 与各
// 槽位/组的 trusted 上（一次连续读失败不得把字段置 0 或“正常”）。
// 本类不创建 Domain 轴、不做语义校验、不写 PLC。
// ============================================================================
#pragma once

#include <memory>
#include <vector>

#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/layout/ReadPlan.h"
#include "infrastructure/plc_vnext/transport/IModbusClient.h"

namespace plc_vnext::telemetry {

class PlcSnapshotReader {
public:
    explicit PlcSnapshotReader(transport::IModbusClientPtr client);

    /// 执行读计划，产出 16 槽位 + 龙门状态快照；失败以 quality 表达，不抛异常。
    [[nodiscard]] contracts::RuntimeSnapshot read();

private:
    /// 读取一个计划区域；任一请求失败或数据不足 → 返回 false。
    bool readRegion(const layout::ReadPlan& plan, std::vector<uint16_t>& out,
                    int expectedWords);

    transport::IModbusClientPtr m_client;
};

}  // namespace plc_vnext::telemetry
