// ============================================================================
// TopologyReadError.h —— Step 6 topology: 启动拓扑发现的失败分类
// ============================================================================
// 只读拓扑读取可能出现的失败类别。PlcTopologyReader 将其映射为
// contracts::ReadResult<TopologySnapshot>::FailureKind（Transport/Decode/
// RevisionChanged），供上层网关统一消费。
//
// 注意：PLC 已成功读出的 ConfigValid=false 属于 TopologySnapshot 的内容，
//       不属于本枚举的失败（见 TDD 实施文档 6.2）。
// ============================================================================
#pragma once

namespace plc_vnext::topology {

enum class TopologyReadError {
    /// 无错误
    None = 0,
    /// TCP / Modbus 通讯失败（含超时、断连）
    TransportFailed,
    /// 读到的寄存器数量/内容不足，无法完成解码
    DecodeFailed,
    /// 双读 Header 的 Revision 不一致（读取期间配置被修改）
    Changed,
    /// SchemaVersion 不受支持（由 TopologyValidator 判定）
    UnsupportedSchema,
    /// ConfigValid=false（PLC 判定配置无效，快照仍保留，由上层决定）
    Invalid,
};

}  // namespace plc_vnext::topology
