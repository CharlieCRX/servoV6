// ============================================================================
// AxisMotionApi.h —— 轴运动"可调用接口"（slot 维度）
// ============================================================================
// 定位 slot2 的方式：按真实 PLC 拓扑把 slot 绑定到某 AxisFunction（如
// Role[2]=Y→slot2），接口经 SystemManagerVnext.system().findBySlot(slot) 解析
// 出 {组, 功能} 后，走 SystemManagerVnext 原子用例（enableAxis/enableMotor/
// triggerAbsMove/triggerRelMove/jog/...，复用 domain 意图校验 + CommandOutbox）。
// 提供两类：
//   - 非阻塞时序（生产）：beginAbs/beginRel/beginJog 返回策略对象，调用方逐帧 tick()；
//   - 阻塞便利（联机调试）：runAbs/runJog 内部 poll()+tick() 跑完。
// 掉电语义：掉电=使能电机 OFF（见方案决策 3）。
// ============================================================================
#pragma once

#include <chrono>
#include <thread>

#include "application_vnext/AppVnextError.h"
#include "application_vnext/SystemManagerVnext.h"
#include "application_vnext/policy/AbsMovePolicy.h"
#include "application_vnext/policy/AxisMotionCommon.h"
#include "application_vnext/policy/JogPolicy.h"
#include "application_vnext/policy/RelMovePolicy.h"
#include "domain_vnext/model/AxisFunction.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"

namespace application_vnext::policy {

class AxisMotionApi {
public:
    explicit AxisMotionApi(SystemManagerVnext& m) : m_(&m) {}

    struct Target {
        plc_vnext::contracts::PlcGroupIndex group{0};
        domain_vnext::model::AxisFunction function = domain_vnext::model::AxisFunction::X;
    };

    /// slot → (组, 功能)。未绑定返回 AxisNotFound。
    AppVnextResult targetFor(plc_vnext::contracts::PlcAxisSlot slot, Target& out) const {
        const auto* axis = m_->system().findBySlot(slot);
        if (!axis) return AxisNotFound{};
        out.group = axis->key().group;
        out.function = axis->key().function;
        return std::monostate{};
    }

    // ---- 独立设置目标（解耦：只写目标，不触发）----
    AppVnextResult setAbsTarget(plc_vnext::contracts::PlcAxisSlot slot, float v) {
        Target t; auto r = targetFor(slot, t);
        return appResultOk(r) ? m_->setAbsTarget(t.function, v) : r;
    }
    AppVnextResult setRelTarget(plc_vnext::contracts::PlcAxisSlot slot, float v) {
        Target t; auto r = targetFor(slot, t);
        return appResultOk(r) ? m_->setRelTarget(t.function, v) : r;
    }

    // ---- 非阻塞时序（生产）：返回策略，调用方逐帧 tick() ----
    AbsMovePolicy beginAbs(plc_vnext::contracts::PlcAxisSlot slot) {
        AbsMovePolicy p(*m_, slot); p.start(); return p;
    }
    RelMovePolicy beginRel(plc_vnext::contracts::PlcAxisSlot slot) {
        RelMovePolicy p(*m_, slot); p.start(); return p;
    }
    JogPolicy beginJog(plc_vnext::contracts::PlcAxisSlot slot, bool forward,
                       int durationMs = 0, int heartbeatPeriodMs = 500) {
        JogPolicy p(*m_, slot, forward, durationMs, heartbeatPeriodMs);
        p.start(); return p;
    }

    // ---- 阻塞便利（联机调试）：内部 setAbsTarget + 触发策略跑完 ----
    MoveOutcome runAbs(plc_vnext::contracts::PlcAxisSlot slot, float target,
                       int timeoutMs = 5000, int pollMs = 50) {
        MoveOutcome o;
        if (!appResultOk(setAbsTarget(slot, target))) {
            o.diag = "setAbsTarget failed (slot 未绑定功能?)";
            return o;
        }
        if (auto* axis = m_->system().findBySlot(slot)) o.startPos = axis->feedback().absPosition;
        auto p = beginAbs(slot);
        if (p.hasError()) { o.diag = p.diag(); return o; }
        p.setVerifyTarget(target);                       // 到位校验（仅判定，不写）
        runUntilDone(p, timeoutMs, pollMs);
        o.ok = p.isDone();
        o.step = AbsMovePolicy::stepName(p.currentStep());
        o.diag = p.diag();
        if (auto* axis = m_->system().findBySlot(slot)) o.endPos = axis->feedback().absPosition;
        return o;
    }

    /// 相对定位阻塞便利：内部 setRelTarget + 触发策略跑完。
    MoveOutcome runRel(plc_vnext::contracts::PlcAxisSlot slot, float delta,
                       int timeoutMs = 5000, int pollMs = 50) {
        MoveOutcome o;
        if (!appResultOk(setRelTarget(slot, delta))) {
            o.diag = "setRelTarget failed (slot 未绑定功能?)";
            return o;
        }
        if (auto* axis = m_->system().findBySlot(slot)) o.startPos = axis->feedback().absPosition;
        auto p = beginRel(slot);
        if (p.hasError()) { o.diag = p.diag(); return o; }
        p.setVerifyTarget(o.startPos + delta);           // 到位校验：起点 + 位移（仅判定，不写）
        runUntilDone(p, timeoutMs, pollMs);
        o.ok = p.isDone();
        o.step = RelMovePolicy::stepName(p.currentStep());
        o.diag = p.diag();
        if (auto* axis = m_->system().findBySlot(slot)) o.endPos = axis->feedback().absPosition;
        return o;
    }

    /// 点动阻塞便利：durationMs 到时自动停止；<=0 视为 3000ms。
    JogOutcome runJog(plc_vnext::contracts::PlcAxisSlot slot, bool forward,
                      int durationMs = 3000, int pollMs = 50,
                      int heartbeatPeriodMs = 500) {
        JogOutcome o;
        if (durationMs <= 0) durationMs = 3000;
        auto p = beginJog(slot, forward, durationMs, heartbeatPeriodMs);
        if (p.hasError()) { o.diag = p.diag(); return o; }
        const int cap = durationMs + 5000;   // 安全上界：运动 + 掉电收尾
        runUntilDone(p, cap, pollMs);
        o.ok = p.isDone();
        o.diag = p.diag();
        return o;
    }

private:
    template <typename Policy>
    void runUntilDone(Policy& p, int timeoutMs, int pollMs) {
        using clock = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::milliseconds(timeoutMs);
        while (clock::now() < deadline && !p.isDone() && !p.hasError()) {
            m_->poll();          // 刷新反馈（motionState）
            p.tick();
            if (p.isDone() || p.hasError()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
        }
    }

    SystemManagerVnext* m_;
};

}  // namespace application_vnext::policy
