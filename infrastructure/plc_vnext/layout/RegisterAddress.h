// ============================================================================
// RegisterAddress.h —— Step 3 layout: 强类型 coil / holding 寄存器地址
// ============================================================================
// 0 基址（PDU 原始地址）：D 区地址等于 D 编号，M 区地址等于 M 编号，不加 1、
// 不加 40001（见《PLC变量协议_Modbus最终地址表.md》§2.1）。
// 强类型区分保持寄存器（D 区）与线圈（M 区），避免把两者混用；后续
// ReadPlan / ReadPlanBuilder 以该类型做类型安全的分区、去重与排序。
// 纯布局：无 Modbus 库 / 网络 / Qt / Domain；不含业务轴名。
// ============================================================================
#pragma once

namespace plc_vnext::layout {

/// D 区保持寄存器地址（Holding Register / FC03·FC06·FC10）。
class HoldingAddress {
public:
    explicit constexpr HoldingAddress(int value) noexcept : value_(value) {}

    [[nodiscard]] constexpr int value() const noexcept { return value_; }

    friend constexpr bool operator==(HoldingAddress a, HoldingAddress b) = default;
    friend constexpr bool operator!=(HoldingAddress a, HoldingAddress b) = default;
    friend constexpr bool operator<(HoldingAddress a, HoldingAddress b) {
        return a.value_ < b.value_;
    }

private:
    int value_;
};

/// M 区线圈地址（Coil / FC01·FC05·FC0F）。
class CoilAddress {
public:
    explicit constexpr CoilAddress(int value) noexcept : value_(value) {}

    [[nodiscard]] constexpr int value() const noexcept { return value_; }

    friend constexpr bool operator==(CoilAddress a, CoilAddress b) = default;
    friend constexpr bool operator!=(CoilAddress a, CoilAddress b) = default;
    friend constexpr bool operator<(CoilAddress a, CoilAddress b) {
        return a.value_ < b.value_;
    }

private:
    int value_;
};

}  // namespace plc_vnext::layout
