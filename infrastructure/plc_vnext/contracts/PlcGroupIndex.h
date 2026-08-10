// ============================================================================
// PlcGroupIndex.h —— Step 1 contracts: 强类型组号
// ============================================================================
// 当前范围 0..1。
//  - tryCreate：与 PlcAxisSlot 同构，越界返回 optional 空值（供解码路径）。
//  - 直接构造：越界抛 std::out_of_range（供强约束的调用方）。
// 纯 DTO：不依赖 Modbus / Qt / Domain。
// ============================================================================
#pragma once

#include <optional>
#include <stdexcept>

namespace plc_vnext::contracts {

class PlcGroupIndex {
public:
    static constexpr int kMin = 0;
    static constexpr int kMax = 1;

    [[nodiscard]] static std::optional<PlcGroupIndex> tryCreate(int v) noexcept {
        if (v < kMin || v > kMax) return std::nullopt;
        return PlcGroupIndex(v);
    }

    explicit PlcGroupIndex(int v) {
        if (v < kMin || v > kMax) {
            throw std::out_of_range("PlcGroupIndex: value out of range [0, 1]");
        }
        v_ = v;
    }

    [[nodiscard]] int value() const { return v_; }

    friend bool operator==(PlcGroupIndex a, PlcGroupIndex b) { return a.v_ == b.v_; }
    friend bool operator!=(PlcGroupIndex a, PlcGroupIndex b) { return !(a == b); }
    friend bool operator<(PlcGroupIndex a, PlcGroupIndex b) { return a.v_ < b.v_; }

private:
    int v_;
};

}  // namespace plc_vnext::contracts
