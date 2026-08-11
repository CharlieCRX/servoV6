// ============================================================================
// ReadPlanBuilder.cpp —— Step 4 layout: 批量读计划构建（纯函数、无 I/O）
// ============================================================================
// 输入契约（违反任一 → 抛 std::invalid_argument，绝不静默忽略）：
//   * RegRange.count >= 1
//   * RegRange.start >= 0
//   * start + count 不得超出 int 表示范围（防止地址溢出）
// 实现：按"区间"排序后贪心合并（相邻或部分重叠）再分片，复杂度 O(R log R)，
//       R 为输入区间数，无需按寄存器逐地址展开，避免大范围时的不必要内存分配。
// ============================================================================
#include "infrastructure/plc_vnext/layout/ReadPlanBuilder.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace plc_vnext::layout {
namespace {

/// 校验单个区间契约；违反抛 std::invalid_argument。
void validate(const RegRange& r) {
    if (r.count < 1) {
        throw std::invalid_argument("ReadPlanBuilder: RegRange.count must be >= 1");
    }
    if (r.start < 0) {
        throw std::invalid_argument("ReadPlanBuilder: RegRange.start must be >= 0");
    }
    // 用 64 位判断 start + count 是否超出 int 可表示范围（防有符号溢出 UB）
    const std::int64_t end = static_cast<std::int64_t>(r.start) + r.count;
    if (end > INT32_MAX) {
        throw std::invalid_argument(
            "ReadPlanBuilder: RegRange [start, start+count) overflows int");
    }
}

/// 把输入区间排序、按相邻/部分重叠合并、再按上限分片。
ReadPlan build(const std::vector<RegRange>& ranges, ReadArea area, int maxPerRequest) {
    struct Seg {
        std::int64_t start;
        std::int64_t end;  // 半开区间 [start, end)
    };

    std::vector<Seg> segs;
    segs.reserve(ranges.size());
    for (const RegRange& r : ranges) {
        validate(r);
        segs.push_back({static_cast<std::int64_t>(r.start),
                        static_cast<std::int64_t>(r.start) + r.count});
    }

    std::sort(segs.begin(), segs.end(),
              [](const Seg& a, const Seg& b) { return a.start < b.start; });

    std::vector<Seg> merged;
    for (const Seg& s : segs) {
        if (merged.empty() || s.start > merged.back().end) {
            merged.push_back(s);  // 不相邻也不重叠 → 新开区间
        } else {
            merged.back().end = std::max(merged.back().end, s.end);  // 相邻/重叠 → 合并
        }
    }

    ReadPlan plan;
    for (const Seg& m : merged) {
        const int segStart = static_cast<int>(m.start);
        const int segLen   = static_cast<int>(m.end - m.start);
        for (int off = 0; off < segLen; off += maxPerRequest) {
            const int nReq = std::min(maxPerRequest, segLen - off);
            plan.push_back(ReadRange{area, segStart + off, nReq});
        }
    }
    return plan;
}

}  // namespace

ReadPlan ReadPlanBuilder::buildHolding(const std::vector<RegRange>& ranges) {
    return build(ranges, ReadArea::Holding, kMaxHoldingRegisters);
}

ReadPlan ReadPlanBuilder::buildCoils(const std::vector<RegRange>& ranges) {
    return build(ranges, ReadArea::Coil, kMaxCoils);
}

}  // namespace plc_vnext::layout
