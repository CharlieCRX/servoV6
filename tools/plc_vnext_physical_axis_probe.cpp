// plc_vnext_physical_axis_probe.cpp — 阶段3 单轴物理调试探针（受控写入）。
// 与正式链路同构（AsioModbusTcpClient->PlcRuntimeGateway->
// PhysicalAxisCommissioningService），对物理 slot 0/1 执行阶段3 受控验证。
// 写入先经写入闸门；普通写需 --confirm-write，运动需 --confirm-motion。
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "application_vnext/commissioning/PhysicalAxisCommissioningService.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/plc_vnext/PlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/contracts/AxisParameterSnapshot.h"
#include "infrastructure/plc_vnext/contracts/AxisRuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"
#include "infrastructure/plc_vnext/transport/AsioModbusTcpClient.h"

namespace {

using plc_vnext::transport::AsioModbusTcpClient;
using namespace application_vnext::commissioning;

struct Args {
    std::string host = "192.168.1.88";
    uint16_t port = 502;
    uint8_t unit = 0x01;
    uint32_t timeoutMs = 1000;
    uint32_t reconnectMs = 2000;
    int slot = 0;
    std::string action = "read";
    float value = 0.f;
    int durationMs = 3000;
    int heartbeatMs = 500;
    bool confirmWrite = false;
    bool confirmMotion = false;
    bool json = false;
};

enum class ParseOutcome { Ok, Help, Error };

bool isWritableAction(const std::string& a) {
    static const char* kWriteActions[] = {
        "set-manual-speed", "set-positioning-speed", "set-abs-target", "set-rel-target",
        "enable-axis", "enable-motor", "jog-forward", "jog-backward",
        "move-relative", "move-absolute", "stop", "release-estop", "trigger-estop"};
    for (const char* wa : kWriteActions) {
        if (a == wa) return true;
    }
    return false;
}

bool isMotionAction(const std::string& a) {
    return a == "jog-forward" || a == "jog-backward" || a == "move-relative" ||
           a == "move-absolute" || a == "stop";
}


int usage(const char* prog) {
    std::printf(
        "usage: %s --host IP --port 502 --unit 1 --slot <2|3> --action <action> \\\n"
        "          [--value X] [--duration-ms 3000] [--heartbeat-ms 500] \\\n"
        "          [--timeout 1000] [--reconnect 2000] \\\n"
        "          [--confirm-write] [--confirm-motion] [--json]\n"
        "actions: read | set-manual-speed | set-positioning-speed | set-abs-target |\n"
        "         set-rel-target | enable-axis | enable-motor | jog-forward |\n"
        "         jog-backward | move-relative | move-absolute | stop |\n"
        "         release-estop | trigger-estop\n"
        "约束: --slot 仅允许 2 或 3（slot 0/1 为 X1/X2 龙门成员，本阶段禁用）；\n"
        "      --confirm-write 才允许普通写入；\n"
        "      --confirm-motion 才允许运动（点动/相对/绝对/停止）。\n",
        prog);
    return 0;
}


ParseOutcome parseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> std::optional<std::string> {
            if (i + 1 < argc) return std::string(argv[++i]);
            return std::nullopt;
        };
        const std::string arg = argv[i];
        if (arg == "--host") { auto v = next(); if (!v) return ParseOutcome::Error; a.host = *v; }
        else if (arg == "--port") { auto v = next(); if (!v) return ParseOutcome::Error; a.port = static_cast<uint16_t>(std::stoi(*v)); }
        else if (arg == "--unit") { auto v = next(); if (!v) return ParseOutcome::Error; a.unit = static_cast<uint8_t>(std::stoi(*v)); }
        else if (arg == "--slot") { auto v = next(); if (!v) return ParseOutcome::Error; a.slot = std::stoi(*v); }
        else if (arg == "--action") { auto v = next(); if (!v) return ParseOutcome::Error; a.action = *v; }
        else if (arg == "--value") { auto v = next(); if (!v) return ParseOutcome::Error; a.value = std::stof(*v); }
        else if (arg == "--duration-ms") { auto v = next(); if (!v) return ParseOutcome::Error; a.durationMs = std::stoi(*v); }
        else if (arg == "--heartbeat-ms") { auto v = next(); if (!v) return ParseOutcome::Error; a.heartbeatMs = std::stoi(*v); }
        else if (arg == "--timeout") { auto v = next(); if (!v) return ParseOutcome::Error; a.timeoutMs = static_cast<uint32_t>(std::stoul(*v)); }
        else if (arg == "--reconnect") { auto v = next(); if (!v) return ParseOutcome::Error; a.reconnectMs = static_cast<uint32_t>(std::stoul(*v)); }
        else if (arg == "--confirm-write") { a.confirmWrite = true; }
        else if (arg == "--confirm-motion") { a.confirmMotion = true; }
        else if (arg == "--json") { a.json = true; }
        else if (arg == "-h" || arg == "--help") { return ParseOutcome::Help; }
        else { std::printf("unknown option: %s\n", argv[i]); return ParseOutcome::Error; }
    }
    static const char* kAllowed[] = {
        "read", "set-manual-speed", "set-positioning-speed", "set-abs-target",
        "set-rel-target", "enable-axis", "enable-motor", "jog-forward",
        "jog-backward", "move-relative", "move-absolute", "stop",
        "release-estop", "trigger-estop"};
    bool ok = false;
    for (const char* aa : kAllowed) { if (a.action == aa) { ok = true; break; } }
    if (!ok) { std::printf("invalid action: %s\n", a.action.c_str()); return ParseOutcome::Error; }
    if (a.slot != 2 && a.slot != 3) {
        std::printf("invalid slot: %d (阶段3仅允许 slot 2 / slot 3；slot 0/1 为 X1/X2 龙门成员禁用)\n", a.slot);
        return ParseOutcome::Error;
    }
    return ParseOutcome::Ok;
}


bool waitConnected(AsioModbusTcpClient& client, unsigned waitMs) {
    const auto t0 = std::chrono::steady_clock::now();
    while (!client.isConnected()) {
        if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(waitMs))
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

const char* motionStateText(int v) {
    static const char* t[] = {"未使能", "空闲", "正向点动", "反向点动", "绝对定位", "相对定位"};
    return (v >= 0 && v < 6) ? t[v] : "?";
}

// 动作分发：返回 0 成功，非 0 失败/被拒。
int runAction(PhysicalAxisCommissioningService& svc, const Args& a) {
    auto slot = plc_vnext::contracts::PlcAxisSlot::tryCreate(a.slot);
    if (!slot) { std::printf("bad slot\n"); return 1; }

    if (a.action == "read") {
        plc_vnext::contracts::AxisRuntimeSnapshot rt;
        plc_vnext::contracts::AxisParameterSnapshot p;
        const bool r1 = svc.readAxisRuntime(*slot, rt);
        const bool r2 = svc.readAxisParameter(*slot, p);
        if (!r1 || !r2) { std::printf("[read] FAILED\n"); return 2; }
        std::printf("[read] slot=%d manualSpeed=%.4f positioningSpeed=%.4f "
                    "absPos=%.4f relPos=%.4f motionState=%d(%s) motionLimit=%d alarmWord=0x%X\n",
                    a.slot, rt.manualSpeed, rt.positioningSpeed, rt.absPosition,
                    rt.relPosition, rt.motionState, motionStateText(rt.motionState),
                    rt.motionLimit, rt.alarmWord);
        std::printf("[read] params: absMoveDistance=%.4f relMoveDistance=%.4f\n",
                    p.absMoveDistance, p.relMoveDistance);
        return 0;
    }

    // 写类动作统一要求 --confirm-write；运动类动作统一要求 --confirm-motion。
    if (isWritableAction(a.action) && !a.confirmWrite) {
        std::printf("[%s] REJECTED: --confirm-write 未提供\n", a.action.c_str());
        return 3;
    }
    if (isMotionAction(a.action) && !a.confirmMotion) {
        std::printf("[%s] REJECTED: --confirm-motion 未提供\n", a.action.c_str());
        return 3;
    }

    // 参数写入/恢复（Step 3.2）。
    if (a.action == "set-manual-speed" || a.action == "set-positioning-speed" ||
        a.action == "set-abs-target" || a.action == "set-rel-target") {
        using K = plc_vnext::contracts::PlcAxisCommandKind;
        const K kind =
            a.action == "set-manual-speed" ? K::SetManualSpeed :
            a.action == "set-positioning-speed" ? K::SetPositioningSpeed :
            a.action == "set-abs-target" ? K::SetAbsTarget : K::SetRelTarget;
        const auto o = svc.verifyParameter(*slot, kind, a.value, a.confirmWrite);
        std::printf("[%s] ok=%d gate=%s original=%.4f written=%.4f readBack=%.4f "
                    "writtenConfirmed=%d restored=%.4f readBackRestored=%.4f "
                    "restoredConfirmed=%d diag=%s\n",
                    a.action.c_str(), o.ok ? 1 : 0, gateReasonText(o.gateReason),
                    o.originalValue, o.writtenValue, o.readBackWritten,
                    o.writtenConfirmed ? 1 : 0, o.restoredValue, o.readBackRestored,
                    o.restoredConfirmed ? 1 : 0, o.diagnostic.c_str());
        return o.ok ? 0 : 4;
    }

    if (a.action == "enable-axis") {
        const auto o = svc.enableAxis(*slot, a.value != 0.f, a.confirmWrite);
        std::printf("[enable-axis] ok=%d gate=%s motionStateAfter=%d diag=%s\n",
                    o.ok ? 1 : 0, gateReasonText(o.gateReason),
                    o.motionStateAfter, o.diagnostic.c_str());
        return o.ok ? 0 : 4;
    }
    if (a.action == "enable-motor") {
        const auto o = svc.enableMotor(*slot, a.value != 0.f, a.confirmWrite);
        std::printf("[enable-motor] ok=%d gate=%s motionStateAfter=%d diag=%s\n",
                    o.ok ? 1 : 0, gateReasonText(o.gateReason),
                    o.motionStateAfter, o.diagnostic.c_str());
        return o.ok ? 0 : 4;
    }
    if (a.action == "jog-forward" || a.action == "jog-backward") {
        const bool forward = a.action == "jog-forward";
        const auto o = svc.jog(*slot, forward, a.durationMs, a.confirmWrite,
                               a.confirmMotion, a.heartbeatMs);
        std::printf("[%s] ok=%d gate=%s startPos=%.4f endPos=%.4f delta=%.4f "
                    "finalMotionState=%d(%s) heartbeatTimeout=%d diag=%s\n",
                    a.action.c_str(), o.ok ? 1 : 0, gateReasonText(o.gateReason),
                    o.startPos, o.endPos, o.endPos - o.startPos,
                    o.finalMotionState, motionStateText(o.finalMotionState),
                    o.heartbeatTimedOut ? 1 : 0, o.diagnostic.c_str());
        return o.ok ? 0 : 4;
    }

    if (a.action == "move-relative") {
        const auto o = svc.moveRelative(*slot, a.value, a.confirmWrite, a.confirmMotion);
        std::printf("[move-relative] ok=%d gate=%s startPos=%.4f endPos=%.4f delta=%.4f "
                    "target=%.4f moved=%d completed=%d finalMotionState=%d diag=%s\n",
                    o.ok ? 1 : 0, gateReasonText(o.gateReason),
                    o.startPos, o.endPos, o.endPos - o.startPos, o.target,
                    o.moved ? 1 : 0, o.completed ? 1 : 0, o.finalMotionState,
                    o.diagnostic.c_str());
        return o.ok ? 0 : 4;
    }
    if (a.action == "move-absolute") {
        const auto o = svc.moveAbsolute(*slot, a.value, a.confirmWrite, a.confirmMotion);
        std::printf("[move-absolute] ok=%d gate=%s startPos=%.4f endPos=%.4f target=%.4f "
                    "moved=%d completed=%d finalMotionState=%d diag=%s\n",
                    o.ok ? 1 : 0, gateReasonText(o.gateReason),
                    o.startPos, o.endPos, o.target, o.moved ? 1 : 0,
                    o.completed ? 1 : 0, o.finalMotionState, o.diagnostic.c_str());
        return o.ok ? 0 : 4;
    }

    if (a.action == "stop") {
        const auto o = svc.stopMove(*slot, a.confirmWrite, a.confirmMotion);
        std::printf("[stop] ok=%d gate=%s finalMotionState=%d diag=%s\n",
                    o.ok ? 1 : 0, gateReasonText(o.gateReason),
                    o.finalMotionState, o.diagnostic.c_str());
        return o.ok ? 0 : 4;
    }
    if (a.action == "release-estop") {
        const auto o = svc.requestReleaseEmergencyStop(a.confirmWrite);
        std::printf("[release-estop] ok=%d diag=%s\n", o.ok ? 1 : 0, o.diagnostic.c_str());
        return o.ok ? 0 : 4;
    }
    if (a.action == "trigger-estop") {
        const auto o = svc.triggerEmergencyStop(a.confirmWrite);
        std::printf("[trigger-estop] ok=%d diag=%s\n", o.ok ? 1 : 0, o.diagnostic.c_str());
        return o.ok ? 0 : 4;
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
        const ParseOutcome o = parseArgs(argc, argv, a);
        if (o == ParseOutcome::Help) { usage(argv[0]); return 0; }
        if (o == ParseOutcome::Error) { usage(argv[0]); return 1; }
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
    PhysicalAxisCommissioningService svc(gateway);

    const int rc = runAction(svc, a);
    client->stop();
    return rc;
}

