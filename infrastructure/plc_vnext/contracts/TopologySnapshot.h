// ============================================================================
// TopologySnapshot.h —— Step 6 contracts: AxisTopology 原始配置快照
// ============================================================================
// 只表达 PLC AxisTopology 的原始配置与客户端校验结果，**不创建 Domain 轴**。
//   - header / groups / roles 全部来自 D1400..D1577 的解码（低字在前 CDAB 字序）。
//   - PLC 的 ConfigValid / ConfigErrorCode 必须原样保留在成功读取的快照中，
//     客户端不得复刻 PLC 的完整业务校验，也不得为本地 TopologyIssue 编造或
//     覆盖 PLC 错误码。
//   - Reserved 字段一并保留（schema 要求写 0），供 TopologyValidator 的
//     客户端解码安全校验使用。
// 纯 DTO：不依赖 Modbus / Qt / Domain；不出现业务轴名。
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace plc_vnext::contracts {

/// AxisTopology 头部（D1400..D1577 内相对基址 D1400 的字段）。
struct TopologyHeader {
    int32_t magic = 0;          ///< D1400..D1401 DINT
    int16_t schemaVersion = 0;  ///< D1402 INT
    int16_t reserved = 0;       ///< D1403 INT，schema 要求写 0
    int32_t revision = 0;       ///< D1404..D1405 DINT（配置提交时递增）
    uint32_t configCRC = 0;     ///< D1574..D1575 DINT（当前固定 0）
    bool configValid = false;   ///< D1576.bit0（PLC 写、只读，原样保留）
    int16_t configErrorCode = 0;///< D1577 INT（PLC 写、只读，原样保留）
};

/// 组内一个角色（ST_RoleAxisBinding，role_base 相对字段）。
struct TopologyRole {
    bool valid = false;          ///< role_base +0.bit0
    bool hmiVisible = false;     ///< role_base +0.bit1
    int16_t plcAxisIndex = -1;   ///< +1 INT；0..15，无效角色 PLC 写 -1
    int16_t motorNo = 0;         ///< +2 INT；虚轴为 0
    int32_t axisClass = 0;       ///< +3..+4 DINT；0线性 1旋转 2虚轴
    int32_t unitType = 0;        ///< +5..+6 DINT；0=mm 1=degree
    int32_t motionMode = 0;      ///< +7..+8 DINT；0未用 1龙门X1 2龙门X2 3线性 4旋转 5逻辑轴
    int16_t reserved = 0;        ///< +9 INT，schema 要求写 0
};

/// 组（ST_GroupAxisMap，group_base 相对字段），含 ABI 全部 8 个 role。
struct TopologyGroup {
    bool valid = false;          ///< group_base +0.bit0
    bool hmiVisible = false;     ///< group_base +0.bit1
    int16_t groupCode = 0;       ///< +1 INT；0=A 1=B
    int16_t reserved = 0;        ///< +2 INT，schema 要求写 0
    std::vector<TopologyRole> roles;  ///< 当前 ABI 8 项（角色数量来自 layout schema 常量）
};

/// 完整拓扑快照（原始配置 + 校验结果）。
struct TopologySnapshot {
    TopologyHeader header;
    std::vector<TopologyGroup> groups;  ///< 当前 2 组（数量来自 layout schema 常量）
};

}  // namespace plc_vnext::contracts
