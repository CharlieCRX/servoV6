// plc_vnext_motion_probe.cpp —— 新策略层运动探针（经 SystemManagerVnext + AxisMotionApi）
// 与正式链路同构（AsioModbusTcpClient->PlcRuntimeGateway->PlcRuntimeDriverAdapter
// ->SystemManagerVnext），按 slot 解析到 AxisFunction 后走原子用例 + 策略时序：
//   - 绝对定位：runAbs(slot, target) 内部"独立写目标 + 触发策略(使能→触发→等待→掉电)"
//   - 相对定位：runRel(slot, delta) 同理
//   - 点动：runJog(slot, dir, durationMs) 使能→点动(心跳)→显式停止→掉电
// 前提：目标 slot 已在真实 PLC 拓扑中绑定到某 AxisFunction（如 Role[2]=Y→slot2）。
// 安全：运动类动作需 --confirm-write 与 --confirm-motion。
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "application_vnext/PlcRuntimeDriverAdapter.h"
#include "application_vnext/SystemManagerVnext.h"
#include "application_vnext/policy/AxisMotionApi.h"
#include "application_vnext/policy/GantryMotionApi.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/plc_vnext/PlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"
#include "infrastructure/plc_vnext/transport/AsioModbusTcpClient.h"

namespace {

using plc_vnext::transport::AsioModbusTcpClient;
using application_vnext::PlcRuntimeDriverAdapter;
using application_vnext::SystemManagerVnext;
using application_vnext::policy::AxisMotionApi;
using application_vnext::policy::GantryMotionApi;
using plc_vnext::contracts::PlcGroupIndex;

struct Args {
    std::string host = "192.168.1.88";
    uint16_t port = 502;
    uint8_t unit = 0x01;
    uint32_t timeoutMs = 1000;
    uint32_t reconnectMs = 2000;
    int slot = 2;
    int group = 0;                 // 龙门组（A 组=0，B 组=1）
    std::string action = "read";
    float value = 0.f;
    int durationMs = 3000;
    int heartbeatMs = 500;
    int moveTimeoutMs = 0;       // 定位动作外部看门狗：0=不限（由策略自身 Done/Error 结束，推荐）
    bool confirmWrite = false;
    bool confirmMotion = false;
};

bool isMotionAction(const std::string& a) {
    return a == "jog-forward" || a == "jog-backward" ||
           a == "move-absolute" || a == "move-relative";
}

bool isGantryLifecycleAction(const std::string& a) {
    return a == "gantry-couple" || a == "gantry-decouple";
}

bool isGantryMotionAction(const std::string& a) {
    return a == "gantry-move-abs" || a == "gantry-move-rel" ||
           a == "gantry-jog-forward" || a == "gantry-jog-backward";
}

bool isGantryRunAction(const std::string& a) {
    return a == "gantry-run-abs" || a == "gantry-run-rel" || a == "gantry-run-jog";
}

int usage(const char* prog) {
    std::printf(
        "usage: %s --host IP --port 502 --unit 1 --slot <已绑定功能的slot，如2> "
        "--action <action> [--value X] [--duration-ms 3000] "
        "[--heartbeat-ms 500] [--timeout 1000] [--reconnect 2000] "
        "[--group 0] [--move-timeout-ms 0] "
        "[--confirm-write] [--confirm-motion]\n"
        "       --move-timeout-ms: 定位外部看门狗，0=不限（默认，由策略自身 Done/Error 结束）；\n"
        "                          定位用时=速度×距离，建议不要设固定上限砍掉慢速移动。\n"
        "       --group: 龙门组（A 组=0，B 组=1，默认 0）。\n"
        "actions: read | topo | jog-forward | jog-backward | move-absolute | move-relative |\n"
        "         gantry-couple | gantry-decouple |\n"
        "         gantry-move-abs | gantry-move-rel | gantry-jog-forward | gantry-jog-backward |\n"
        "         gantry-run-abs | gantry-run-rel | gantry-run-jog\n"
        "       topo: 打印当前 PLC 拓扑中已绑定功能的 slot 映射（排查 slot 未注册）\n"
        "       gantry-couple: 建立联动并使能逻辑轴（->Ready）；gantry-decouple: 解除并掉电逻辑轴\n"
        "       gantry-move-abs/rel/jog: 龙门下逻辑轴运动（前提已 gantry-couple 到 Ready）\n"
        "       gantry-run-abs/rel/jog: 组合闭环：自动 建立+使能 -> 运动 -> 解除+掉电\n"
        "约束: slot 必须已在拓扑绑定到某 AxisFunction（否则 AxisNotFound）；\n"
        "      单轴/龙门运动类动作需 --confirm-write 与 --confirm-motion；\n"
        "      龙门建立/解除需 --confirm-write。\n",
        prog);
    return 0;
}

int parseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> std::optional<std::string> {
            if (i + 1 < argc) return std::string(argv[++i]);
            return std::nullopt;
        };
        const std::string arg = argv[i];
        if (arg == "--host") { auto v = next(); if (!v) return 1; a.host = *v; }
        else if (arg == "--port") { auto v = next(); if (!v) return 1; a.port = static_cast<uint16_t>(std::stoi(*v)); }
        else if (arg == "--unit") { auto v = next(); if (!v) return 1; a.unit = static_cast<uint8_t>(std::stoi(*v)); }
        else if (arg == "--slot") { auto v = next(); if (!v) return 1; a.slot = std::stoi(*v); }
        else if (arg == "--group") { auto v = next(); if (!v) return 1; a.group = std::stoi(*v); }
        else if (arg == "--action") { auto v = next(); if (!v) return 1; a.action = *v; }
        else if (arg == "--value") { auto v = next(); if (!v) return 1; a.value = std::stof(*v); }
        else if (arg == "--duration-ms") { auto v = next(); if (!v) return 1; a.durationMs = std::stoi(*v); }
        else if (arg == "--heartbeat-ms") { auto v = next(); if (!v) return 1; a.heartbeatMs = std::stoi(*v); }
        else if (arg == "--timeout") { auto v = next(); if (!v) return 1; a.timeoutMs = static_cast<uint32_t>(std::stoul(*v)); }
        else if (arg == "--move-timeout-ms") { auto v = next(); if (!v) return 1; a.moveTimeoutMs = std::stoi(*v); }
        else if (arg == "--reconnect") { auto v = next(); if (!v) return 1; a.reconnectMs = static_cast<uint32_t>(std::stoul(*v)); }
        else if (arg == "--confirm-write") { a.confirmWrite = true; }
        else if (arg == "--confirm-motion") { a.confirmMotion = true; }
        else { std::printf("unknown arg: %s\n", arg.c_str()); return 1; }
    }
    return 0;
}

bool waitConnected(AsioModbusTcpClient& c, int ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (c.isConnected()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return c.isConnected();
}

/// 同步急停：读取 M224/M225 注入安全状态机，解除"未同步"锁定。
void syncSafety(plc_vnext::PlcRuntimeGateway& gw, SystemManagerVnext& mgr) {
    auto s = gw.readSafety();
    mgr.applyEmergencyStopFeedback(s.hasValue() && s.value().emergencyStop);
}

/// 读取某槽位当前 motionState（供诊断打印；未注册返回 -1）。
int16_t slotMotion(SystemManagerVnext& mgr, plc_vnext::contracts::PlcAxisSlot slot) {
    const auto* axis = mgr.system().findBySlot(slot);
    return axis ? axis->feedback().motionState : -1;
}

/// 手动逐帧驱动策略，逐步打印 step + motionState + 绝对位置，精确定位卡点。
template <typename Policy>
int driveVerbose(SystemManagerVnext& mgr, plc_vnext::contracts::PlcAxisSlot slot,
                 Policy& p, int capMs, const char* name,
                 float target = std::numeric_limits<float>::quiet_NaN()) {
    auto posOf = [&]() -> float {
        const auto* a = mgr.system().findBySlot(slot);
        return a ? a->feedback().absPosition : 0.f;
    };
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(capMs);
    const float startPos = posOf();
    std::printf("[%s] begin step=%s ms=%d pos=%.2f", name,
                Policy::stepName(p.currentStep()), (int)slotMotion(mgr, slot), startPos);
    if (!std::isnan(target)) std::printf(" (目标 %.2f)", target);
    std::printf("\n");
    int steps = 0;
    // capMs<=0 = 不限：驱动到策略自身 Done/Error（策略内已有"从未启动/未到位"安全超时）。
    while ((capMs <= 0 || clock::now() < deadline) && !p.isDone() && !p.hasError()) {
        mgr.poll();
        p.tick();
        ++steps;
        if ((steps % 5) == 0 || p.isDone() || p.hasError()) {
            std::printf("  step=%-16s ms=%d pos=%.2f\n", Policy::stepName(p.currentStep()),
                        (int)slotMotion(mgr, slot), posOf());
        }
        if (p.isDone() || p.hasError()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const bool ok = p.isDone();
    const float endPos = posOf();
    std::printf("[%s] end ok=%d step=%s ms=%d pos %.2f -> %.2f (Δ=%.2f) diag=%s\n",
                name, ok ? 1 : 0, Policy::stepName(p.currentStep()),
                (int)slotMotion(mgr, slot), startPos, endPos, endPos - startPos,
                p.diag().c_str());
    return ok ? 0 : 4;
}

//__PART3__

/// 解析某组的逻辑轴（X）槽位；未绑定返回 nullopt。
std::optional<plc_vnext::contracts::PlcAxisSlot> logicalSlotOf(
    SystemManagerVnext& mgr, PlcGroupIndex g) {
    const auto* a = mgr.system().find({g, domain_vnext::model::AxisFunction::X});
    return a ? std::optional(a->slot()) : std::nullopt;
}

/// 手动逐帧驱动龙门生命周期策略（couple/decouple），打印 step + gantryState +
/// internalStep + 逻辑轴 ms/pos。completeWhenReady=true 以 isReady() 判完成（couple），
/// false 以 isDone() 判完成（decouple）。
template <typename Policy>
int driveGantryLifecycle(SystemManagerVnext& mgr, PlcGroupIndex g, Policy& p,
                         int capMs, const char* name, bool completeWhenReady) {
    auto stateOf = [&]() -> int16_t { return mgr.gantryStatus(g).rawState; };
    auto stepOf = [&]() -> int16_t { return mgr.gantryStatus(g).internalStep; };
    auto msOf = [&]() -> int16_t {
        const auto* a = mgr.system().find({g, domain_vnext::model::AxisFunction::X});
        return a ? a->feedback().motionState : -1;
    };
    auto posOf = [&]() -> float {
        const auto* a = mgr.system().find({g, domain_vnext::model::AxisFunction::X});
        return a ? a->feedback().absPosition : 0.f;
    };
    const auto done = [&]() { return completeWhenReady ? p.isReady() : p.isDone(); };

    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(capMs);
    std::printf("[%s] begin step=%s gantry=%d iStep=%d ms=%d pos=%.2f\n", name,
                Policy::stepName(p.currentStep()), (int)stateOf(), (int)stepOf(),
                (int)msOf(), posOf());
    int steps = 0;
    while ((capMs <= 0 || clock::now() < deadline) && !done() && !p.hasError()) {
        mgr.poll();
        p.tick();
        ++steps;
        if ((steps % 5) == 0 || done() || p.hasError()) {
            std::printf("  step=%-16s gantry=%d iStep=%d ms=%d pos=%.2f\n",
                        Policy::stepName(p.currentStep()), (int)stateOf(),
                        (int)stepOf(), (int)msOf(), posOf());
        }
        if (done() || p.hasError()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const bool ok = done();
    std::printf("[%s] end ok=%d step=%s gantry=%d iStep=%d ms=%d pos=%.2f diag=%s\n",
                name, ok ? 1 : 0, Policy::stepName(p.currentStep()), (int)stateOf(),
                (int)stepOf(), (int)msOf(), posOf(), p.diag().c_str());
    return ok ? 0 : 4;
}

int runAction(SystemManagerVnext& mgr, AxisMotionApi& api, const Args& a) {
    auto slot = plc_vnext::contracts::PlcAxisSlot::tryCreate(a.slot);
    if (!slot) { std::printf("[%s] bad slot: %d\n", a.action.c_str(), a.slot); return 1; }

    if (a.action == "read") {
        AxisMotionApi::Target t;
        auto r = api.targetFor(*slot, t);
        if (!appResultOk(r)) { std::printf("[read] slot %d 未绑定 AxisFunction\n", a.slot); return 2; }
        const auto* axis = mgr.system().findBySlot(*slot);
        const auto& fb = axis->feedback();
        std::printf("[read] slot=%d function=%s motionState=%d absPos=%.4f "
                    "alarmWord=0x%X\n",
                    a.slot, domain_vnext::model::axisFunctionName(t.function).data(),
                    fb.motionState, fb.absPosition, fb.alarmWord);
        return 0;
    }

    // 打印拓扑绑定情况：已注册的 slot → 功能。排查"slot 未注册"。
    if (a.action == "topo") {
        std::printf("[topo] 已绑定功能的 slot（其余槽位未注册，需在 PLC 拓扑中绑定）：\n");
        const auto& reg = mgr.system().registry();
        for (const auto s : reg.occupiedSlots()) {
            const auto* axis = reg.find(s);
            const auto fn = axis ? axis->key().function : domain_vnext::model::AxisFunction::X;
            std::printf("  slot=%d -> %s\n", s.value(),
                        domain_vnext::model::axisFunctionName(fn).data());
        }
        std::printf("[topo] 总轴数=%zu / 16\n", reg.count());
        return 0;
    }

    if (isMotionAction(a.action)) {
        if (!a.confirmWrite) { std::printf("[%s] REJECTED: 需 --confirm-write\n", a.action.c_str()); return 3; }
        if (!a.confirmMotion) { std::printf("[%s] REJECTED: 需 --confirm-motion\n", a.action.c_str()); return 3; }
    }

    if (a.action == "move-absolute") {
        if (!appResultOk(api.setAbsTarget(*slot, a.value))) {
            std::printf("[move-absolute] setAbsTarget 失败（slot 未绑定?）\n");
            return 4;
        }
        auto p = api.beginAbs(*slot);
        p.setVerifyTarget(a.value);
        return driveVerbose(mgr, *slot, p, /*capMs=*/a.moveTimeoutMs, "move-absolute", a.value);
    }
    if (a.action == "move-relative") {
        if (!appResultOk(api.setRelTarget(*slot, a.value))) {
            std::printf("[move-relative] setRelTarget 失败（slot 未绑定?）\n");
            return 4;
        }
        const auto* axis = mgr.system().findBySlot(*slot);
        const float startPos = axis ? axis->feedback().absPosition : 0.f;
        auto p = api.beginRel(*slot);
        p.setVerifyTarget(startPos + a.value);
        return driveVerbose(mgr, *slot, p, /*capMs=*/a.moveTimeoutMs, "move-relative",
                            startPos + a.value);
    }
    if (a.action == "jog-forward" || a.action == "jog-backward") {
        const bool fwd = a.action == "jog-forward";
        auto p = api.beginJog(*slot, fwd, a.durationMs, a.heartbeatMs);
        return driveVerbose(mgr, *slot, p, /*capMs=*/a.moveTimeoutMs, a.action.c_str());
    }

    // ============ 龙门联动 ============
    if (a.group < 0 || a.group > 1) {
        std::printf("[%s] bad group: %d（仅 0/1）\n", a.action.c_str(), a.group);
        return 1;
    }
    const PlcGroupIndex g(a.group);

    // 注入龙门配置有效标志（假定 PLC 已配置 GantryParam；若配置无效 PLC 会拒绝 couple）。
    // 这是 GantryCouplingStateMachine::requestCouple 的 configValid 准入来源。
    domain_vnext::model::GantryParamModel gcfg;
    gcfg.valid = true;
    mgr.applyGantryConfig(g, gcfg);

    // 龙门生命周期：建立联动并使能逻辑轴 / 解除联动并掉电逻辑轴。
    if (isGantryLifecycleAction(a.action)) {
        if (!a.confirmWrite) {
            std::printf("[%s] REJECTED: 需 --confirm-write\n", a.action.c_str());
            return 3;
        }
        GantryMotionApi gapi(mgr);
        if (a.action == "gantry-couple") {
            auto p = gapi.beginEnableAndCouple(g);
            return driveGantryLifecycle(mgr, g, p, a.moveTimeoutMs,
                                        "gantry-couple", /*completeWhenReady=*/true);
        }
        auto p = gapi.beginDecoupleAndDisable(g);
        return driveGantryLifecycle(mgr, g, p, a.moveTimeoutMs,
                                    "gantry-decouple", /*completeWhenReady=*/false);
    }

    // 龙门逻辑轴运动：前提已 gantry-couple 到 Ready（LogicalControlAllowed）。
    if (isGantryMotionAction(a.action)) {
        if (!a.confirmWrite || !a.confirmMotion) {
            std::printf("[%s] REJECTED: 需 --confirm-write 与 --confirm-motion\n",
                        a.action.c_str());
            return 3;
        }
        auto lslot = logicalSlotOf(mgr, g);
        if (!lslot) {
            std::printf("[%s] 逻辑轴 X 未绑定（组 %d）\n", a.action.c_str(), a.group);
            return 4;
        }
        GantryMotionApi gapi(mgr);
        if (a.action == "gantry-move-abs") {
            if (!appResultOk(gapi.setAbsTarget(g, a.value))) {
                std::printf("[gantry-move-abs] setAbsTarget 失败（逻辑轴未绑定?）\n");
                return 4;
            }
            auto p = gapi.beginAbs(g);
            if (p.hasError()) { std::printf("  diag=%s\n", p.diag().c_str()); return 4; }
            p.setVerifyTarget(a.value);
            return driveVerbose(mgr, *lslot, p, /*capMs=*/a.moveTimeoutMs,
                                "gantry-move-abs", a.value);
        }
        if (a.action == "gantry-move-rel") {
            if (!appResultOk(gapi.setRelTarget(g, a.value))) {
                std::printf("[gantry-move-rel] setRelTarget 失败（逻辑轴未绑定?）\n");
                return 4;
            }
            auto p = gapi.beginRel(g);
            if (p.hasError()) { std::printf("  diag=%s\n", p.diag().c_str()); return 4; }
            const auto* ax = mgr.system().findBySlot(*lslot);
            const float startPos = ax ? ax->feedback().absPosition : 0.f;
            p.setVerifyTarget(startPos + a.value);
            return driveVerbose(mgr, *lslot, p, /*capMs=*/a.moveTimeoutMs,
                                "gantry-move-rel", startPos + a.value);
        }
        // gantry-jog-forward / gantry-jog-backward
        const bool fwd = a.action == "gantry-jog-forward";
        auto p = gapi.beginJog(g, fwd, a.durationMs, a.heartbeatMs);
        if (p.hasError()) { std::printf("  diag=%s\n", p.diag().c_str()); return 4; }
        return driveVerbose(mgr, *lslot, p, /*capMs=*/a.moveTimeoutMs, a.action.c_str());
    }

    // 组合闭环：自动 建立+使能 -> 运动 -> 解除+掉电（每次点动/位置移动都全自动）。
    if (isGantryRunAction(a.action)) {
        if (!a.confirmWrite || !a.confirmMotion) {
            std::printf("[%s] REJECTED: 需 --confirm-write 与 --confirm-motion\n",
                        a.action.c_str());
            return 3;
        }
        auto lslot = logicalSlotOf(mgr, g);
        if (!lslot) {
            std::printf("[%s] 逻辑轴 X 未绑定（组 %d）\n", a.action.c_str(), a.group);
            return 4;
        }
        GantryMotionApi gapi(mgr);

        // [1] 建立联动并使能逻辑轴（->Ready）。失败则中止（不运动、不解除）。
        {
            auto p = gapi.beginEnableAndCouple(g);
            const int rc = driveGantryLifecycle(mgr, g, p, a.moveTimeoutMs,
                                                "run[1]couple", /*ready=*/true);
            if (rc != 0) {
                std::printf("[%s] 建立联动失败，中止。\n", a.action.c_str());
                return rc;
            }
        }

        // [2] 龙门下运动（定位或点动）。
        int rc = 0;
        if (a.action == "gantry-run-abs") {
            if (!appResultOk(gapi.setAbsTarget(g, a.value))) {
                std::printf("[gantry-run-abs] setAbsTarget 失败\n"); rc = 4;
            } else {
                auto p = gapi.beginAbs(g);
                if (p.hasError()) { std::printf("  diag=%s\n", p.diag().c_str()); rc = 4; }
                else {
                    p.setVerifyTarget(a.value);
                    rc = driveVerbose(mgr, *lslot, p, a.moveTimeoutMs,
                                      "run[2]move-abs", a.value);
                }
            }
        } else if (a.action == "gantry-run-rel") {
            if (!appResultOk(gapi.setRelTarget(g, a.value))) {
                std::printf("[gantry-run-rel] setRelTarget 失败\n"); rc = 4;
            } else {
                auto p = gapi.beginRel(g);
                if (p.hasError()) { std::printf("  diag=%s\n", p.diag().c_str()); rc = 4; }
                else {
                    const auto* ax = mgr.system().findBySlot(*lslot);
                    const float startPos = ax ? ax->feedback().absPosition : 0.f;
                    p.setVerifyTarget(startPos + a.value);
                    rc = driveVerbose(mgr, *lslot, p, a.moveTimeoutMs,
                                      "run[2]move-rel", startPos + a.value);
                }
            }
        } else {  // gantry-run-jog（正向点动）
            auto p = gapi.beginJog(g, /*forward=*/true, a.durationMs, a.heartbeatMs);
            if (p.hasError()) { std::printf("  diag=%s\n", p.diag().c_str()); rc = 4; }
            else { rc = driveVerbose(mgr, *lslot, p, a.moveTimeoutMs, "run[2]jog"); }
        }

        // [3] 解除联动并掉电（无论运动成败，都要安全解除）。
        {
            auto p = gapi.beginDecoupleAndDisable(g);
            const int rc2 = driveGantryLifecycle(mgr, g, p, a.moveTimeoutMs,
                                                 "run[3]decouple", /*ready=*/false);
            if (rc2 != 0) {
                std::printf("[%s] 警告：解除联动失败：%s\n", a.action.c_str(),
                            p.diag().c_str());
                if (rc == 0) rc = rc2;
            }
        }
        return rc;
    }

    std::printf("unhandled action: %s\n", a.action.c_str());
    return 1;
}
}  // namespace

int main(int argc, char** argv) {
    LoggerConfig lc;
    lc.enableConsole = false;
    lc.enableFile = false;
    Logger::init(lc);
    struct LoggerShutdownGuard { ~LoggerShutdownGuard() { Logger::shutdown(); } } loggerGuard;

    Args a;
    if (argc > 1) {
        if (parseArgs(argc, argv, a) != 0) { usage(argv[0]); return 1; }
        if (a.action == "help") { usage(argv[0]); return 0; }
    }

    AsioModbusTcpClient::Config cfg;
    cfg.host = a.host;
    cfg.port = a.port;
    cfg.unitId = a.unit;
    cfg.timeoutMs = a.timeoutMs;
    cfg.reconnectIntervalMs = a.reconnectMs;
    auto client = std::make_shared<AsioModbusTcpClient>(cfg);
    client->start();
    if (!waitConnected(*client, 3000)) {
        std::printf("连接失败/超时：%s:%u（3000ms 内未建立 TCP 连接）。\n",
                    a.host.c_str(), a.port);
        client->stop();
        return 2;
    }

    plc_vnext::PlcRuntimeGateway gateway(client);
    PlcRuntimeDriverAdapter adapter(gateway);
    SystemManagerVnext mgr(adapter);
    if (!mgr.boot()) {
        std::printf("boot 失败：readTopology 失败或拓扑无效。\n");
        client->stop();
        return 2;
    }
    syncSafety(gateway, mgr);

    AxisMotionApi api(mgr);
    const int rc = runAction(mgr, api, a);
    client->stop();
    return rc;
}
