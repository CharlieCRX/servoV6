// ============================================================================
// PhysicalAxisCommissioningService.h —— 阶段3：单轴物理调试服务（Step 3.0）
// ============================================================================
// 与 SystemManagerVnext（龙门 X1/X2 逻辑轴路由）并行、独立存在。本服务**不**从
// UI 直接调用底层 writeAxis()，也不复用会经过逻辑轴路由的 X1/X2 正式入口；所有
// 写一律先经写入闸门（PLC 已连接 / 拓扑读取成功 / Magic·SchemaVersion 正确 /
// ConfigValid=true / Revision 未变化 / Runtime trusted / Safety trusted /
// M224=false / slot 仅 0 或 1 / 目标轴无报警 / 无其它点动会话），再直接：
//     gateway.writeAxis(PlcAxisSlot(0 或 1), command)
// 闸门拒绝返回明确 CommissioningGateReason，且被拒绝的请求**不产生任何写报文**。
// 本服务只允许 slot 0、slot 1；禁止 B 组、slot 2~15、逻辑轴 slot13、龙门联动、
// AckSeq/CommandResult 控制（那些走 SystemManagerVnext，不在本服务能力内）。
//
// 写结果语义：writeAxis().ok() 只证明写请求到达 PLC；运动/参数最终必须以 PLC
// 读回确认（readRuntime / readAxisParameters），不把“写成功”当作“执行成功”。
// 纯 C++：只依赖 plc_vnext 的 IPlcRuntimeGateway 接口（可注入 Fake 做离线测试）。
// ============================================================================
#pragma once

#include <atomic>
#include <optional>
#include <string>

#include "application_vnext/commissioning/CommissioningTypes.h"
#include "infrastructure/plc_vnext/IPlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/contracts/AxisParameterSnapshot.h"
#include "infrastructure/plc_vnext/contracts/AxisRuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"

namespace application_vnext::commissioning {

class PhysicalAxisCommissioningService {
public:
    explicit PhysicalAxisCommissioningService(plc_vnext::IPlcRuntimeGateway& gateway);

    // ---- 闸门（Step 3.0）----
    struct GateResult {
        CommissioningGateReason reason = CommissioningGateReason::Ok;
        [[nodiscard]] bool allow() const {
            return reason == CommissioningGateReason::Ok;
        }
    };
    /// 强制刷新门控输入（读连接 / 拓扑 / 运行 / 急停）并评估本次操作是否允许。
    GateResult evaluateGate(plc_vnext::contracts::PlcAxisSlot slot,
                            CommissioningOperation op) const;

    // ---- 参数写入 / 恢复（Step 3.2）----
    struct ParameterVerifyOutcome {
        bool ok = false;
        CommissioningGateReason gateReason = CommissioningGateReason::Ok;
        float originalValue = 0.f;     ///< 读到的原值
        float writtenValue = 0.f;      ///< 计划写入的测试值
        float readBackWritten = 0.f;   ///< 写入后读回值
        float restoredValue = 0.f;     ///< 计划恢复的原值
        float readBackRestored = 0.f;  ///< 恢复后读回值
        bool writtenConfirmed = false; ///< 读回值 == 测试值
        bool restoredConfirmed = false;///< 读回值 == 原值
        std::string diagnostic;
    };
    /// 读原值 → 写测试值 → 读回确认 → 恢复原值 → 读回确认。
    ParameterVerifyOutcome verifyParameter(plc_vnext::contracts::PlcAxisSlot slot,
                                           plc_vnext::contracts::PlcAxisCommandKind kind,
                                           float testValue, bool confirmWrite) const;

    // ---- 使能（Step 3.3）----
    struct EnableOutcome {
        bool ok = false;
        CommissioningGateReason gateReason = CommissioningGateReason::Ok;
        int16_t motionStateAfter = 0;
        std::string diagnostic;
    };
    EnableOutcome enableAxis(plc_vnext::contracts::PlcAxisSlot slot, bool on,
                             bool confirmWrite) const;
    EnableOutcome enableMotor(plc_vnext::contracts::PlcAxisSlot slot, bool on,
                              bool confirmWrite) const;

    // ---- 点动（Step 3.4，经 JogSession）----
    struct JogOutcome {
        bool ok = false;
        CommissioningGateReason gateReason = CommissioningGateReason::Ok;
        float startPos = 0.f;
        float endPos = 0.f;
        int16_t finalMotionState = 0;
        bool heartbeatTimedOut = false;  ///< 心跳写失败导致会话退出
        std::string diagnostic;
    };
    JogOutcome jog(plc_vnext::contracts::PlcAxisSlot slot, bool forward,
                   int durationMs, bool confirmWrite, bool confirmMotion,
                   int heartbeatPeriodMs = 500) const;

    // ---- 运动（Step 3.5 / 3.6）与停止（Step 3.7）----
    struct MoveOutcome {
        bool ok = false;
        CommissioningGateReason gateReason = CommissioningGateReason::Ok;
        float startPos = 0.f;
        float endPos = 0.f;
        float target = 0.f;
        bool moved = false;            ///< 实际位移是否超过容差
        bool completed = false;        ///< 运动回到空闲状态
        int16_t finalMotionState = 0;
        std::string diagnostic;
    };
    MoveOutcome moveRelative(plc_vnext::contracts::PlcAxisSlot slot, float delta,
                             bool confirmWrite, bool confirmMotion) const;
    MoveOutcome moveAbsolute(plc_vnext::contracts::PlcAxisSlot slot, float target,
                             bool confirmWrite, bool confirmMotion) const;
    /// 定位停止：只写 StopAbsMove / StopRelMove（PLC 自复位，只写 ON）。
    MoveOutcome stopMove(plc_vnext::contracts::PlcAxisSlot slot, bool confirmWrite,
                         bool confirmMotion) const;

    // ---- 急停（Step 3.8）----
    struct EStopOutcome {
        bool ok = false;
        std::string diagnostic;
    };
    /// 请求解除急停：写 M225=ON（急停期间唯一允许的写；解除后 M224/M225 自动 OFF，
    /// 轴不自动恢复使能 / 运动，必须重新 EnableAxis + EnableMotor + 重新下发命令）。
    EStopOutcome requestReleaseEmergencyStop(bool confirmWrite) const;
    /// 触发设备急停（测试用）：写 M224=ON（锁存）。
    EStopOutcome triggerEmergencyStop(bool confirmWrite) const;

    // ---- 只读辅助 ----
    /// 读取某槽位运行反馈；失败 / 不可信返回 false。
    bool readAxisRuntime(plc_vnext::contracts::PlcAxisSlot slot,
                         plc_vnext::contracts::AxisRuntimeSnapshot& out) const;
    /// 读取某槽位参数区（RW）；失败 / 不可信返回 false。
    bool readAxisParameter(plc_vnext::contracts::PlcAxisSlot slot,
                           plc_vnext::contracts::AxisParameterSnapshot& out) const;

    /// 当前是否处于点动会话（闸门 Busy 依据）。
    bool jogActive() const { return jogActive_.load(); }

private:
    /// 本阶段物理单轴白名单：仅 slot 2 / slot 3。
    /// 现场 slot 0/1 为 X1/X2 龙门成员（独立使能会与龙门逻辑轴路由冲突，且解除
    /// 联动属龙门操作，本阶段禁用）；slot 2/3 为独立物理轴，可直接单轴调试。
    [[nodiscard]] bool slotAllowed(plc_vnext::contracts::PlcAxisSlot slot) const {
        return slot.value() == 2 || slot.value() == 3;
    }
    /// 统一单轴写入口：先闸门，再 gateway.writeAxis(slot, cmd)。
    /// 失败时写 reason 与 diag；成功返回 ok()。
    plc_vnext::contracts::CommunicationResult write(
        plc_vnext::contracts::PlcAxisSlot slot,
        const plc_vnext::contracts::PlcAxisCommand& cmd,
        CommissioningOperation op, bool confirmWrite,
        CommissioningGateReason& reason, std::string& diag) const;
    /// 读取某参数当前值（按命令类别选择读回来源）。
    bool readParameterValue(plc_vnext::contracts::PlcAxisSlot slot,
                            plc_vnext::contracts::PlcAxisCommandKind kind,
                            float& value) const;
    /// 读取目标槽位当前绝对位置（EU）；失败返回 false。
    bool currentAbsPosition(plc_vnext::contracts::PlcAxisSlot slot, float& pos) const;
    /// 轮询直到 motionState == 空闲(1) 或超时；返回是否回到空闲。
    bool waitUntilIdle(plc_vnext::contracts::PlcAxisSlot slot, int timeoutMs,
                       int pollMs) const;

    plc_vnext::IPlcRuntimeGateway& gateway_;
    /// 上次成功读取的拓扑 Revision（用于 Revision 变化检测）。
    mutable std::optional<int32_t> lastRevision_;
    mutable std::atomic<bool> jogActive_{false};
};

}  // namespace application_vnext::commissioning
