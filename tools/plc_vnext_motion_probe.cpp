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
#include "infrastructure/logger/Logger.h"
#include "infrastructure/plc_vnext/PlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/transport/AsioModbusTcpClient.h"

namespace {

using plc_vnext::transport::AsioModbusTcpClient;
using application_vnext::PlcRuntimeDriverAdapter;
using application_vnext::SystemManagerVnext;
using application_vnext::policy::AxisMotionApi;

struct Args {
    std::string host = "192.168.1.88";
    uint16_t port = 502;
    uint8_t unit = 0x01;
    uint32_t timeoutMs = 1000;
    uint32_t reconnectMs = 2000;
    int slot = 2;
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

int usage(const char* prog) {
    std::printf(
        "usage: %s --host IP --port 502 --unit 1 --slot <已绑定功能的slot，如2> "
        "--action <action> [--value X] [--duration-ms 3000] "
        "[--heartbeat-ms 500] [--timeout 1000] [--reconnect 2000] "
        "[--move-timeout-ms 0] "
        "[--confirm-write] [--confirm-motion]\n"
        "       --move-timeout-ms: 定位外部看门狗，0=不限（默认，由策略自身 Done/Error 结束）；\n"
        "                          定位用时=速度×距离，建议不要设固定上限砍掉慢速移动。\n"
        "actions: read | topo | jog-forward | jog-backward | move-absolute | move-relative\n"
        "       topo: 打印当前 PLC 拓扑中已绑定功能的 slot 映射（排查 slot 未注册）\n"
        "约束: slot 必须已在拓扑绑定到某 AxisFunction（否则 AxisNotFound）；\n"
        "      运动类动作需 --confirm-write 与 --confirm-motion。\n",
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
