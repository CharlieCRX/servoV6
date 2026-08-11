// ============================================================================
// ReadPlanBuilder.h —— Step 4 layout: 由 layout 生成最少的安全批量读取请求
// ============================================================================
// 纯函数、无 I/O。算法复用旧 plc::protocol::AddressPacker 的合并思路
// （见 PlcPoller），但改为纯函数输入输出、不依赖 RegisterRegistry，且按区间
// 排序合并而非逐地址展开（复杂度 O(R log R)，R 为输入区间数）：
//   1. 校验输入区间契约（start ≥ 0、count ≥ 1、start + count 不溢出 int），
//      违反任一抛 std::invalid_argument，绝不静默忽略
//   2. 按 start 排序
//   3. 相邻 / 部分重叠区间合并为一段
//   4. 超过协议上限自动分片（FC03 ≤ 125 / FC01 ≤ 2000）
// 与 tools/plc_read_validate.py 的分片逻辑一致：D 区按 125 分片、M 区一次读完。
// 纯布局：无 Modbus 库 / 网络 / Qt / Domain；不识别槽位/轴/业务。
// ============================================================================
#pragma once

#include <vector>

#include "infrastructure/plc_vnext/layout/ReadPlan.h"

namespace plc_vnext::layout {

/// 原始输入：一个待读的连续地址区间（start 为 0 基址，count ≥ 1）。
/// 契约：start ≥ 0、count ≥ 1、start + count 不超出 int；违反任一 → 抛
/// std::invalid_argument（非法输入绝不静默忽略）。
struct RegRange {
    int start;
    int count;
};

/// 把 layout 给出的散列地址合并为最少的、每片合法的批量读取请求。
/// 复杂度 O(R log R)，R = 输入区间数：按区间排序后贪心合并（相邻或部分重叠）
/// 再分片，无需按寄存器逐地址展开，避免大范围时的不必要内存分配。
class ReadPlanBuilder {
public:
    static constexpr int kMaxHoldingRegisters = 125;   // FC03 单次读取上限
    static constexpr int kMaxCoils            = 2000;  // FC01 单次读取上限

    /// 合并保持寄存器（D 区）区间 → 最少的 FC03 请求（每片 ≤ 125）。
    [[nodiscard]] static ReadPlan buildHolding(const std::vector<RegRange>& ranges);

    /// 合并线圈（M 区）区间 → 最少的 FC01 请求（每片 ≤ 2000）。
    [[nodiscard]] static ReadPlan buildCoils(const std::vector<RegRange>& ranges);
};

}  // namespace plc_vnext::layout
