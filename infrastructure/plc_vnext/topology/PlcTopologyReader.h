// ============================================================================
// PlcTopologyReader.h —— Step 6 topology: Header→Body→Header 双读读取器
// ============================================================================
// 按 §4.1 双读流程读取 AxisTopology（只读，无写路径）：
//   1. 读取 Header（Magic/Schema/Revision，D1400..D1405）
//   2. 读取完整 Body（178 字）——经 Step 4 ReadPlanBuilder 拆为合法 FC03 分片
//      （≤125 每片：D1400..D1524 与 D1525..D1577），按序拼回 178 字再解码
//   3. 再次读取 Header
//   4. Revision 不一致 → TopologyReadError::Changed（映射 ReadResult::RevisionChanged）
//   5. 接入 TopologyValidator 客户端解码安全校验：
//        - 客户端安全问题（Schema/槽位/重复/组码/保留字段）→ Decode 失败
//        - ConfigValid=false 仍成功返回快照（原样保留，交由上层决策）
//
// 读取失败统一返回 contracts::ReadResult<TopologySnapshot>（Transport/Decode/
// RevisionChanged），不抛异常。本类不创建 Domain 轴。
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "infrastructure/plc_vnext/contracts/ReadResult.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"
#include "infrastructure/plc_vnext/transport/IModbusClient.h"
#include "infrastructure/plc_vnext/topology/TopologyReadError.h"

namespace plc_vnext::topology {

class PlcTopologyReader {
public:
    explicit PlcTopologyReader(transport::IModbusClientPtr client);

    /// 双读校验并校验拓扑后返回快照；失败返回对应 FailureKind。
    [[nodiscard]] contracts::ReadResult<contracts::TopologySnapshot> read();

private:
    /// 读取 D1400..D1405 头部并解码 Revision；失败返回 false。
    bool readRevision(int32_t& out);

    [[nodiscard]] contracts::ReadResult<contracts::TopologySnapshot> makeFailure(
        TopologyReadError error, const std::string& diagnostic);

    transport::IModbusClientPtr m_client;
};

}  // namespace plc_vnext::topology
