// ============================================================================
// PlcAxisSlot.h —— Step 1 contracts: 强类型槽位
// ============================================================================
// 范围 0..15。构造路径仅 tryCreate（返回 optional）：
// PLC 读出的整数必须经 tryCreate 转为槽位；转换失败返回空值，由调用方
// 作为可诊断的解码/拓扑问题处理，绝不在此以 C++ 异常中止轮询线程。
// 纯 DTO：不依赖 Modbus / Qt / Domain。
// ============================================================================
#pragma once

#include <optional>

namespace plc_vnext::contracts {

class PlcAxisSlot {
public:
    static constexpr int kMin = 0;
    static constexpr int kMax = 15;

    [[nodiscard]] static std::optional<PlcAxisSlot> tryCreate(int v) noexcept {
        if (v < kMin || v > kMax) return std::nullopt;
        return PlcAxisSlot(v);
    }

    [[nodiscard]] int value() const { return v_; }

    friend bool operator==(PlcAxisSlot a, PlcAxisSlot b) { return a.v_ == b.v_; }
    friend bool operator!=(PlcAxisSlot a, PlcAxisSlot b) { return !(a == b); }
    friend bool operator<(PlcAxisSlot a, PlcAxisSlot b) { return a.v_ < b.v_; }

private:
    explicit PlcAxisSlot(int v) noexcept : v_(v) {}
    int v_;
};

}  // namespace plc_vnext::contracts
