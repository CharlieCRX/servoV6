// ============================================================================
// PlcCommand.h —— Step 8 command: 单轴写入意图的协议无关表达
// ============================================================================
// 只表达"application 想对某个 PLC 槽位做什么"（写目标/触发/使能/点动/清除），
// 不含地址、不含编码、不含"是否应该运动"的判定（那是 application 的职责）。
// 写入分类（保持电平/边沿/自复位）见 command/CommandWritePolicy.h。
// 纯 DTO：不依赖 Modbus / Qt / Domain；不 include 旧 plc::protocol。
// ============================================================================
#pragma once

namespace plc_vnext::contracts {

/// 单轴命令类别。命名与《PLC变量协议_Modbus最终地址表.md》§3.1/§3.2 对齐。
enum class PlcAxisCommandKind {
    // ---- 保持寄存器参数写（REAL，低字在前）----
    SetManualSpeed,       // 手动速度 D(0+2s)
    SetPositioningSpeed,  // 定位速度 D(32+2s)
    SetAbsTarget,         // 绝对定位距离 D(1096+2s)
    SetRelTarget,         // 相对定位距离 D(1128+2s)

    // ---- 保持电平线圈（本机负责最终 OFF）----
    EnableAxis,           // M(0+i)    使能轴控
    EnableMotor,          // M(128+i)  使能电机
    JogForward,           // M(80+i)   点动正转
    JogBackward,          // M(96+i)   点动反转
    JogHeartbeat,         // M(192+i)  点动心跳（周期写 ON）

    // ---- PLC 自复位（只写 ON；PLC 自动复位读回 OFF）----
    // 触发/终止线圈由 PLC 当前版本自动复位，客户端无需配对 OFF（仅需读回确认）。
    TriggerAbsMove,       // M(48+i)   绝对定位触发
    TriggerRelMove,       // M(64+i)   相对定位触发
    StopAbsMove,          // M(160+i)  绝对定位终止
    StopRelMove,          // M(144+i)  相对定位终止
    ClearAbsPosition,     // M(32+i)   绝对位置清零
    ClearRelZero,         // M(16+i)   相对原点清除
    SetRelZero,           // M(176+i)  相对原点设置

    // ---- PLC 自复位（后续地址表补充，见《Domain层重构设计》§7.1）----
    ResetAlarm,           // M(112+i)  报警解除触发（当前 PLC 未实现；映射层按能力拒绝）
    ClearAlarmWord,       // M(208+i)  告警码置零

    // ---- RW 参数区写（后续地址表补充，见《Domain层重构设计》§7.1）----
    SetRelZeroRecord,     // D(1064+2s)  相对原点记录
    SetSoftNegLimit,      // D(1160+2s)  软件负限位
    SetSoftPosLimit,      // D(1192+2s)  软件正限位
    SetSoftLimitControl,  // D(1228+s)   软限位控制（WORD，bit0正 bit1负）
};

/// 协议无关的单轴写入意图。槽位（0..15）由调用方单独以 contracts::PlcAxisSlot 传入。
struct PlcAxisCommand {
    PlcAxisCommandKind kind = PlcAxisCommandKind::SetManualSpeed;
    float realValue = 0.0f;  ///< 用于 Set* 参数写（REAL）
    bool boolValue = false;  ///< 用于保持电平线圈（Enable*/Jog*/JogHeartbeat）

    static PlcAxisCommand makeSetManualSpeed(float v) {
        return {PlcAxisCommandKind::SetManualSpeed, v, false};
    }
    static PlcAxisCommand makeSetPositioningSpeed(float v) {
        return {PlcAxisCommandKind::SetPositioningSpeed, v, false};
    }
    static PlcAxisCommand makeSetAbsTarget(float v) {
        return {PlcAxisCommandKind::SetAbsTarget, v, false};
    }
    static PlcAxisCommand makeSetRelTarget(float v) {
        return {PlcAxisCommandKind::SetRelTarget, v, false};
    }

    static PlcAxisCommand makeEnableAxis(bool v) {
        return {PlcAxisCommandKind::EnableAxis, 0.0f, v};
    }
    static PlcAxisCommand makeEnableMotor(bool v) {
        return {PlcAxisCommandKind::EnableMotor, 0.0f, v};
    }
    static PlcAxisCommand makeJogForward(bool v) {
        return {PlcAxisCommandKind::JogForward, 0.0f, v};
    }
    static PlcAxisCommand makeJogBackward(bool v) {
        return {PlcAxisCommandKind::JogBackward, 0.0f, v};
    }
    static PlcAxisCommand makeJogHeartbeat(bool on) {
        return {PlcAxisCommandKind::JogHeartbeat, 0.0f, on};
    }

    static PlcAxisCommand makeTriggerAbsMove() {
        return {PlcAxisCommandKind::TriggerAbsMove, 0.0f, false};
    }
    static PlcAxisCommand makeTriggerRelMove() {
        return {PlcAxisCommandKind::TriggerRelMove, 0.0f, false};
    }
    static PlcAxisCommand makeStopAbsMove() {
        return {PlcAxisCommandKind::StopAbsMove, 0.0f, false};
    }
    static PlcAxisCommand makeStopRelMove() {
        return {PlcAxisCommandKind::StopRelMove, 0.0f, false};
    }

    static PlcAxisCommand makeClearAbsPosition() {
        return {PlcAxisCommandKind::ClearAbsPosition, 0.0f, false};
    }
    static PlcAxisCommand makeClearRelZero() {
        return {PlcAxisCommandKind::ClearRelZero, 0.0f, false};
    }
    static PlcAxisCommand makeSetRelZero() {
        return {PlcAxisCommandKind::SetRelZero, 0.0f, false};
    }

    // ---- 后续地址表补充命令（见《Domain层重构设计》§7.1）----
    static PlcAxisCommand makeResetAlarm() {
        return {PlcAxisCommandKind::ResetAlarm, 0.0f, false};
    }
    static PlcAxisCommand makeClearAlarmWord() {
        return {PlcAxisCommandKind::ClearAlarmWord, 0.0f, false};
    }
    static PlcAxisCommand makeSetRelZeroRecord(float v) {
        return {PlcAxisCommandKind::SetRelZeroRecord, v, false};
    }
    static PlcAxisCommand makeSetSoftNegLimit(float v) {
        return {PlcAxisCommandKind::SetSoftNegLimit, v, false};
    }
    static PlcAxisCommand makeSetSoftPosLimit(float v) {
        return {PlcAxisCommandKind::SetSoftPosLimit, v, false};
    }
    /// 软限位控制字（D1228，WORD bit0正 bit1负）。realValue 承载原始 WORD 值。
    static PlcAxisCommand makeSetSoftLimitControl(bool positiveEnabled,
                                                  bool negativeEnabled) {
        const float raw = static_cast<float>(
            (positiveEnabled ? 0x01u : 0u) | (negativeEnabled ? 0x02u : 0u));
        return {PlcAxisCommandKind::SetSoftLimitControl, raw, false};
    }
};

}  // namespace plc_vnext::contracts
