// ============================================================================
// ReadPlan.h —— Step 4 layout: 批量读计划（合并连续区间后的读请求集合）
// ============================================================================
// 目标：把"需要轮询的全部地址"合并为最少且合法的批量读取请求：
//   FC03（保持寄存器/D 区）每片 ≤ 125；FC01（线圈/M 区）每片 ≤ 2000。
// 纯布局 / 纯数据：无 Modbus 库 / 网络 / Qt / Domain；不识别槽位/轴/业务。
// ReadRange.area 使下游 ModbusIoExecutor 可按区域选择对应功能码。
// ============================================================================
#pragma once

#include <cstdint>
#include <ostream>
#include <vector>

namespace plc_vnext::layout {

/// 读取区域：保持寄存器（D 区 / FC03）或线圈（M 区 / FC01）。
enum class ReadArea {
    Holding,  // FC03 保持寄存器
    Coil,     // FC01 线圈
};

/// 一个合法的批量读取请求区间（0 基址）。
struct ReadRange {
    ReadArea area;
    int      start;
    int      count;

    friend constexpr bool operator==(ReadRange a, ReadRange b) = default;
};

inline std::ostream& operator<<(std::ostream& os, ReadRange r) {
    return os << "ReadRange{area="
              << (r.area == ReadArea::Holding ? "Holding" : "Coil")
              << ", start=" << r.start << ", count=" << r.count << "}";
}

/// 完整的读计划：最少的、每片合法的批量读取请求集合。
using ReadPlan = std::vector<ReadRange>;

}  // namespace plc_vnext::layout
