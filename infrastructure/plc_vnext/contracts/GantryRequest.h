// ============================================================================
// GantryRequest.h —— Step 9 command: 龙门请求（组事务）的协议无关表达
// ============================================================================
// 一个龙门请求建模为 A 组事务：SYN0(X) + X1 + X2，经 GantryCommand 提交，由
// AckSeq + State + InGear 共同确认（确认/超时判定属 Step 10 telemetry/ack reader，
// 本 DTO 与 writer 不参与）。本文件只表达"application 想让某个组做什么"：
//   GantryCommandKind   Couple=1 / Decouple=2 / Reset=3（协议编码，见地址表 §7）
//   requestSeq          application/session 提供的 N+1，writer 原样写入、不自增
//
// 依据《PLC龙门联动控制逻辑》§3.1/§3.5：命令不保持、不加 ON/OFF 脉冲；重复相同
// 命令必须使用新的 RequestSeq。递增序号、去重、准入检查均属上层（session/Gateway），
// writer 不复制这些判断（见 TDD 文档 §9.3）。
// 纯 DTO：不依赖 Modbus / Qt / Domain；不 include 旧 plc::protocol。
// ============================================================================
#pragma once

#include <cstdint>

namespace plc_vnext::contracts {

/// 龙门命令码。与《PLC龙门联动控制逻辑》§3.1 的 GANTRY_CMD_* 对齐。
enum class GantryCommandKind : int16_t {
    None     = 0,   // GANTRY_CMD_NONE      无命令
    Couple   = 1,   // GANTRY_CMD_COUPLE    建立联动
    Decouple = 2,   // GANTRY_CMD_DECOUPLE  解除联动
    Reset    = 3,   // GANTRY_CMD_RESET     联动故障复位
};

/// 协议无关的龙门请求。组号（0..1）由调用方单独以 contracts::PlcGroupIndex 传入。
struct GantryRequest {
    GantryCommandKind command = GantryCommandKind::None;
    /// application 提供的 N+1；writer 原样写入 D181..D182（DINT，CDAB 低字在前），
    /// 不自行生成、不递增、不跨请求维护状态。
    int32_t requestSeq = 0;

    static GantryRequest couple(int32_t seq)   { return {GantryCommandKind::Couple, seq}; }
    static GantryRequest decouple(int32_t seq) { return {GantryCommandKind::Decouple, seq}; }
    static GantryRequest reset(int32_t seq)    { return {GantryCommandKind::Reset, seq}; }

    /// 协议编码值（INT，0/1/2/3），供 writer 编码进 Command 寄存器。
    [[nodiscard]] int16_t commandCode() const { return static_cast<int16_t>(command); }
};

}  // namespace plc_vnext::contracts
