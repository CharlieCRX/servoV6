// ============================================================================
// GantryMotionApi.h —— 龙门联动运动的"可调用接口"（业务意图维度）
// ============================================================================
// 不要把"写 GantryCommand=1/2/3"暴露给 UI/普通 AxisMotionApi。本 API 只暴露
// 业务意图，内部按 组 -> AxisFunction::X -> 拓扑解析 逻辑轴 slot（A 组=13），
// 未来 B 组无需改动运动策略。
// 结构：
//   - 生命周期：beginEnableAndCouple / beginDecoupleAndDisable（GantryLifecyclePolicy，
//     拥有电源：建立/复位/解除/掉电）。
//   - 运动：beginAbs/beginRel/beginJog —— 以 LifecycleManaged 电源模式 + 注入
//     GantryMotionGuard（运行中持续校验逻辑轴许可，失联即停，不直接掉电）。
//   - 目标/停止：setAbsTarget/setRelTarget/setPositioningSpeed/stop 直接落到逻辑轴 X。
// 只控制逻辑轴 X；不允许把 X1/X2 作为已联动下的运动目标。
// 非阻塞（生产）：beginXxx 返回策略，调用方主 poll 循环先 poll() 再 tick()。
// 阻塞（联机调试）：runXxx 内部 poll()+tick() 跑完。
// ============================================================================
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <thread>

#include "application_vnext/AppVnextError.h"
#include "application_vnext/SystemManagerVnext.h"
#include "application_vnext/policy/AbsMovePolicy.h"
#include "application_vnext/policy/AxisMotionCommon.h"
#include "application_vnext/policy/GantryLifecyclePolicy.h"
#include "application_vnext/policy/GantryMotionGuard.h"
#include "application_vnext/policy/JogPolicy.h"
#include "application_vnext/policy/RelMovePolicy.h"
#include "domain_vnext/model/AxisFunction.h"
#include "domain_vnext/model/AxisKey.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

namespace application_vnext::policy {

class GantryMotionApi {
public:
    explicit GantryMotionApi(SystemManagerVnext& m)
        : m_(&m),
          guards_{GantryMotionGuard(m, plc_vnext::contracts::PlcGroupIndex(0)),
                  GantryMotionGuard(m, plc_vnext::contracts::PlcGroupIndex(1))} {}

    // ---- 生命周期（拥有电源）----
    GantryLifecyclePolicy beginEnableAndCouple(plc_vnext::contracts::PlcGroupIndex g) {
        GantryLifecyclePolicy p(*m_, g); p.beginCouple(); return p;
    }
    GantryLifecyclePolicy beginDecoupleAndDisable(plc_vnext::contracts::PlcGroupIndex g) {
        GantryLifecyclePolicy p(*m_, g); p.beginDecouple(); return p;
    }

    // ---- 目标 / 速度 / 停止（写逻辑轴 X）----
    AppVnextResult setPositioningSpeed(plc_vnext::contracts::PlcGroupIndex g, float v) {
        return m_->setPositioningSpeed(g, domain_vnext::model::AxisFunction::X, v);
    }
    AppVnextResult setAbsTarget(plc_vnext::contracts::PlcGroupIndex g, float v) {
        return m_->setAbsTarget(g, domain_vnext::model::AxisFunction::X, v);
    }
    AppVnextResult setRelTarget(plc_vnext::contracts::PlcGroupIndex g, float v) {
        return m_->setRelTarget(g, domain_vnext::model::AxisFunction::X, v);
    }
    AppVnextResult stop(plc_vnext::contracts::PlcGroupIndex g) {
        return m_->stop(g, domain_vnext::model::AxisFunction::X);
    }

    // ---- 运动（LifecycleManaged + guard）----
    AbsMovePolicy beginAbs(plc_vnext::contracts::PlcGroupIndex g) {
        plc_vnext::contracts::PlcAxisSlot slot = *plc_vnext::contracts::PlcAxisSlot::tryCreate(0);
        if (!logicalSlot(g, slot)) {
            // 不绑定逻辑轴：返回立即 Error 的策略，绝不误控任何有效 PLC 槽位。
            AbsMovePolicy bad(*m_, *plc_vnext::contracts::PlcAxisSlot::tryCreate(0));
            bad.setUnavailable("logical axis X not bound in group");
            return bad;
        }
        AbsMovePolicy p(*m_, slot);
        p.setPowerOwnership(PowerOwnership::LifecycleManaged);
        p.setGantryGuard(&guards_[static_cast<std::size_t>(g.value())]);
        p.start();
        return p;
    }
    RelMovePolicy beginRel(plc_vnext::contracts::PlcGroupIndex g) {
        plc_vnext::contracts::PlcAxisSlot slot = *plc_vnext::contracts::PlcAxisSlot::tryCreate(0);
        if (!logicalSlot(g, slot)) {
            RelMovePolicy bad(*m_, *plc_vnext::contracts::PlcAxisSlot::tryCreate(0));
            bad.setUnavailable("logical axis X not bound in group");
            return bad;
        }
        RelMovePolicy p(*m_, slot);
        p.setPowerOwnership(PowerOwnership::LifecycleManaged);
        p.setGantryGuard(&guards_[static_cast<std::size_t>(g.value())]);
        p.start();
        return p;
    }
    JogPolicy beginJog(plc_vnext::contracts::PlcGroupIndex g, bool forward,
                       int durationMs = 0, int heartbeatPeriodMs = 500) {
        plc_vnext::contracts::PlcAxisSlot slot = *plc_vnext::contracts::PlcAxisSlot::tryCreate(0);
        if (!logicalSlot(g, slot)) {
            JogPolicy bad(*m_, *plc_vnext::contracts::PlcAxisSlot::tryCreate(0),
                          forward, durationMs, heartbeatPeriodMs);
            bad.setUnavailable("logical axis X not bound in group");
            return bad;
        }
        JogPolicy p(*m_, slot, forward, durationMs, heartbeatPeriodMs);
        p.setPowerOwnership(PowerOwnership::LifecycleManaged);
        p.setGantryGuard(&guards_[static_cast<std::size_t>(g.value())]);
        p.start();
        return p;
    }

    // ---- 阻塞便利（联机调试）----
    MoveOutcome runAbs(plc_vnext::contracts::PlcGroupIndex g, float target,
                       int timeoutMs = 8000, int pollMs = 50) {
        MoveOutcome o;
        if (!appResultOk(setAbsTarget(g, target))) {
            o.diag = "setAbsTarget failed (逻辑轴 X 未绑定?)";
            return o;
        }
        auto p = beginAbs(g);
        if (p.hasError()) { o.diag = p.diag(); return o; }
        p.setVerifyTarget(target);
        runUntilDone(p, timeoutMs, pollMs);
        o.ok = p.isDone();
        o.step = AbsMovePolicy::stepName(p.currentStep());
        o.diag = p.diag();
        return o;
    }
    MoveOutcome runRel(plc_vnext::contracts::PlcGroupIndex g, float delta,
                       int timeoutMs = 8000, int pollMs = 50) {
        MoveOutcome o;
        if (!appResultOk(setRelTarget(g, delta))) {
            o.diag = "setRelTarget failed (逻辑轴 X 未绑定?)";
            return o;
        }
        auto p = beginRel(g);
        if (p.hasError()) { o.diag = p.diag(); return o; }
        // 到位校验目标应为"起点 + 相对位移"，而非把 delta 直接当绝对位置。
        float startPos = 0.f;
        {
            plc_vnext::contracts::PlcAxisSlot s = *plc_vnext::contracts::PlcAxisSlot::tryCreate(0);
            if (logicalSlot(g, s)) {
                if (const auto* a = m_->system().findBySlot(s)) startPos = a->feedback().absPosition;
            }
        }
        p.setVerifyTarget(startPos + delta);
        runUntilDone(p, timeoutMs, pollMs);
        o.ok = p.isDone();
        o.step = RelMovePolicy::stepName(p.currentStep());
        o.diag = p.diag();
        return o;
    }
    JogOutcome runJog(plc_vnext::contracts::PlcGroupIndex g, bool forward,
                      int durationMs, int pollMs = 50, int heartbeatPeriodMs = 500) {
        JogOutcome o;
        if (durationMs <= 0) durationMs = 3000;
        auto p = beginJog(g, forward, durationMs, heartbeatPeriodMs);
        if (p.hasError()) { o.diag = p.diag(); return o; }
        runUntilDone(p, durationMs + 5000, pollMs);
        o.ok = p.isDone();
        o.diag = p.diag();
        return o;
    }

private:
    /// 组 -> 逻辑轴 X 槽位。失败返回 false（调用方据此返回"未可用"策略）。
    bool logicalSlot(plc_vnext::contracts::PlcGroupIndex g,
                     plc_vnext::contracts::PlcAxisSlot& out) const {
        const auto* a = m_->system().find({g, domain_vnext::model::AxisFunction::X});
        if (!a) return false;
        out = a->slot();
        return true;
    }

    template <typename Policy>
    void runUntilDone(Policy& p, int timeoutMs, int pollMs) {
        using clock = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::milliseconds(timeoutMs);
        while (clock::now() < deadline && !p.isDone() && !p.hasError()) {
            m_->poll();
            p.tick();
            if (p.isDone() || p.hasError()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
        }
    }

    SystemManagerVnext* m_;
    std::array<GantryMotionGuard, 2> guards_;
};

}  // namespace application_vnext::policy

