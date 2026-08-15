// plc_vnext_udp_probe.cpp —— Phase 7 联机探针：统一协调层「submit → tick → 快照」链路
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 7：
//   「UDP 触发 → 快照 → UI 同步」链路验证。与 UDP/UI 走同一条入口
//   MotionControlService::submit()（无 Qt / UDP server 依赖），随后由唯一 tick 循环
//   驱动仲裁/执行/会话，并读 ControlStateStore 快照验证状态同步。
// 链路：AsioModbusTcpClient -> PlcRuntimeGateway -> PlcRuntimeDriverAdapter
//   -> MotionControlService(driver, GatewayControlRuntime(runtime))  // 唯一协调器
// 动作：read | gantry-couple | gantry-decouple | gantry-move-abs | gantry-move-rel |
//       move-abs | move-rel | jog-forward | jog-backward | stop
// 安全：写需 --confirm-write；运动类还需 --confirm-motion。
// ============================================================================
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "application_vnext/PlcRuntimeDriverAdapter.h"
#include "application_vnext/control/GatewayControlRuntime.h"
#include "application_vnext/control/MotionControlService.h"
#include "domain_vnext/model/AxisFunction.h"
#include "domain_vnext/model/GantryParam.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/plc_vnext/PlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"
#include "infrastructure/plc_vnext/transport/AsioModbusTcpClient.h"

namespace {

using application_vnext::PlcRuntimeDriverAdapter;
using application_vnext::control::MotionControlService;
using application_vnext::control::ControlCommand;
using application_vnext::control::ControlSource;
using application_vnext::control::ControlAction;
using application_vnext::control::MotionRequest;
using application_vnext::control::OperationState;
using plc_vnext::transport::AsioModbusTcpClient;
using plc_vnext::contracts::PlcGroupIndex;
using domain_vnext::model::AxisFunction;

/// OperationState -> 可读名（探针本地实现，避免依赖旧 UDP 层的 Qt）。
inline const char* stateName(OperationState s) {
    switch (s) {
        case OperationState::Queued:           return "Queued";
        case OperationState::Accepted:         return "Accepted";
        case OperationState::Rejected:         return "Rejected";
        case OperationState::Running:          return "Running";
        case OperationState::Succeeded:        return "Succeeded";
        case OperationState::Failed:           return "Failed";
        case OperationState::Cancelled:        return "Cancelled";
        case OperationState::TimedOut:         return "TimedOut";
        case OperationState::CommitUncertain:  return "CommitUncertain";
    }
    return "?";
}

struct Args {
    std::string host = "192.168.1.88";
    uint16_t port = 502;
    uint8_t unit = 0x01;
    uint32_t timeoutMs = 1000;
    uint32_t reconnectMs = 2000;
    int group = 0;
    std::string function = "X";
    std::string action = "read";
    float value = 0.f;
    float speed = 10.f;
    int durationMs = 3000;
    int capMs = 0;
    bool confirmWrite = false;
    bool confirmMotion = false;
};

bool isWriteAction(const std::string& a) {
    return a == "gantry-couple" || a == "gantry-decouple" || a == "gantry-move-abs" ||
           a == "gantry-move-rel" || a == "move-abs" || a == "move-rel" ||
           a == "jog-forward" || a == "jog-backward" || a == "stop";
}
bool isMotionAction(const std::string& a) {
    return a == "gantry-move-abs" || a == "gantry-move-rel" || a == "move-abs" ||
           a == "move-rel" || a == "jog-forward" || a == "jog-backward";
}

std::optional<AxisFunction> parseFunction(const std::string& s) {
    if (s == "X")  return AxisFunction::X;
    if (s == "X1") return AxisFunction::X1;
    if (s == "X2") return AxisFunction::X2;
    if (s == "Y")  return AxisFunction::Y;
    if (s == "Z")  return AxisFunction::Z;
    if (s == "R")  return AxisFunction::R;
    return std::nullopt;
}

int usage(const char* prog) {
    std::printf(
        "usage: %s --host IP --port 502 --unit 1 --group 0 --function X "
        "--action <action> [--value X] [--speed V] [--duration-ms 3000] "
        "[--cap-ms 0] [--timeout 1000] [--reconnect 2000] "
        "[--confirm-write] [--confirm-motion]\n"
        "       --cap-ms: 会话外部看门狗；0=不限（默认，由会话自身终局结束）。\n"
        "actions: read | gantry-couple | gantry-decouple | gantry-move-abs | "
        "gantry-move-rel | move-abs | move-rel | jog-forward | jog-backward | stop\n"
        "       gantry-* 走龙门生命周期/逻辑轴（申请 gantry:A:0 组租约，同组独占）；\n"
        "       普通 move-abs/rel 为单轴运动。所有运动经 MotionControlService 唯一\n"
        "       submit->tick->快照 链路（与 UDP/UI 同入口）。\n"
        "约束: 写类动作需 --confirm-write；运动类还需 --confirm-motion。\n",
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
        else if (arg == "--group") { auto v = next(); if (!v) return 1; a.group = std::stoi(*v); }
        else if (arg == "--function") { auto v = next(); if (!v) return 1; a.function = *v; }
        else if (arg == "--action") { auto v = next(); if (!v) return 1; a.action = *v; }
        else if (arg == "--value") { auto v = next(); if (!v) return 1; a.value = std::stof(*v); }
        else if (arg == "--speed") { auto v = next(); if (!v) return 1; a.speed = std::stof(*v); }
        else if (arg == "--duration-ms") { auto v = next(); if (!v) return 1; a.durationMs = std::stoi(*v); }
        else if (arg == "--cap-ms") { auto v = next(); if (!v) return 1; a.capMs = std::stoi(*v); }
        else if (arg == "--timeout") { auto v = next(); if (!v) return 1; a.timeoutMs = static_cast<uint32_t>(std::stoul(*v)); }
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

void printSnapshot(MotionControlService& svc) {
    const auto snap = svc.store().snapshot();
    std::printf("[snapshot] conn=%d safetyTrusted=%d estop=%d globallyLocked=%d\n",
                snap.connection.connected ? 1 : 0, snap.safety.trusted ? 1 : 0,
                snap.safety.emergencyStop ? 1 : 0, svc.globallyLocked() ? 1 : 0);
    for (std::size_t g = 0; g < snap.gantries.size(); ++g) {
        const auto& gu = snap.gantries[g];
        std::printf("[snapshot] gantry[%zu] trusted=%d state=%d iStep=%d cmdResult=%d "
                    "err=%d logical=%d member=%d x1=%d x2=%d lease=%d\n",
                    g, gu.trusted ? 1 : 0, (int)gu.state, (int)gu.internalStep,
                    (int)gu.commandResult, (int)gu.commandErrorCode,
                    gu.logicalControlAllowed ? 1 : 0, gu.memberControlAllowed ? 1 : 0,
                    gu.x1InGear ? 1 : 0, gu.x2InGear ? 1 : 0,
                    gu.lifecycleLeased ? 1 : 0);
    }
    for (std::size_t i = 0; i < snap.axes.size(); ++i) {
        const auto& a = snap.axes[i];
        if (!a.bound) continue;
        std::printf("[snapshot] axis[%zu] %c.%s slot=%d ms=%d pos=%.2f leased=%d owner=%s\n",
                    i, (a.group.value() == 0) ? 'A' : 'B',
                    domain_vnext::model::axisFunctionName(a.role).data(), a.slot,
                    (int)a.motionState, a.absPosition, a.leased ? 1 : 0,
                    a.leaseOwnerName.c_str());
    }
    for (const auto& op : snap.operations) {
        std::printf("[snapshot] op %s state=%s axis=%s diag=%s\n", op.operationId.c_str(),
                    stateName(op.state), op.axis.c_str(), op.diag.c_str());
    }
}

int driveOperation(MotionControlService& svc, const std::string& opId, int capMs,
                   const char* name, int jogDurationMs = 0) {
    auto isTerminal = [](OperationState s) {
        return s == OperationState::Succeeded || s == OperationState::Failed ||
               s == OperationState::Rejected || s == OperationState::TimedOut ||
               s == OperationState::Cancelled || s == OperationState::CommitUncertain;
    };
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(capMs);
    OperationState prev = OperationState::Queued;
    OperationState final = OperationState::Queued;
    bool jogStopSent = false;
    const auto jogDeadline = jogDurationMs > 0
        ? clock::now() + std::chrono::milliseconds(jogDurationMs) : clock::time_point{};

    std::printf("[%s] submitted op=%s\n", name, opId.c_str());
    while (capMs <= 0 || clock::now() < deadline) {
        svc.tick();
        const auto op = svc.queryOperation(opId);
        if (!op) break;
        if (op->state != prev) {
            std::printf("[%s] op %s state=%s%s\n", name, opId.c_str(), stateName(op->state),
                        op->diag.empty() ? "" : (std::string(" diag=") + op->diag).c_str());
            prev = op->state;
        }
        if (jogDurationMs > 0 && !jogStopSent && clock::now() >= jogDeadline) {
            ControlCommand stop;
            stop.source = ControlSource::Udp;
            stop.action = ControlAction::StopMotion;
            svc.submit(stop);
            jogStopSent = true;
            std::printf("[%s] jog 时长到，提交 StopMotion\n", name);
        }
        if (isTerminal(op->state)) { final = op->state; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (prev != final && isTerminal(final)) {
        std::printf("[%s] final op=%s state=%s\n", name, opId.c_str(), stateName(final));
    }
    const bool ok = (final == OperationState::Succeeded);
    std::printf("[%s] result=%s\n", name, ok ? "Succeeded" : stateName(final));
    return ok ? 0 : 4;
}

int run(MotionControlService& svc, const Args& a) {
    const auto fnOpt = parseFunction(a.function);
    if (!fnOpt) { std::printf("bad function: %s\n", a.function.c_str()); return 1; }
    const AxisFunction fn = *fnOpt;
    const PlcGroupIndex g(a.group);

    if (a.action == "read") { printSnapshot(svc); return 0; }

    if (isWriteAction(a.action)) {
        if (!a.confirmWrite) { std::printf("[%s] REJECTED: 需 --confirm-write\n", a.action.c_str()); return 3; }
    }
    if (isMotionAction(a.action)) {
        if (!a.confirmWrite || !a.confirmMotion) {
            std::printf("[%s] REJECTED: 需 --confirm-write 与 --confirm-motion\n", a.action.c_str());
            return 3;
        }
    }

    if (a.action.rfind("gantry-", 0) == 0) {
        domain_vnext::model::GantryParamModel gcfg;
        gcfg.valid = true;
        svc.applyGantryConfig(g, gcfg);
    }

    ControlCommand cmd;
    cmd.source = ControlSource::Udp;
    cmd.target.group = g;
    cmd.target.function = fn;

    if (a.action == "gantry-couple") {
        cmd.action = ControlAction::GantryEnableAndCouple;
    } else if (a.action == "gantry-decouple") {
        cmd.action = ControlAction::GantryDecoupleAndDisable;
    } else if (a.action == "gantry-move-abs" || a.action == "move-abs") {
        cmd.action = ControlAction::StartAbsMove;
        cmd.motion = MotionRequest{a.value, a.speed};
    } else if (a.action == "gantry-move-rel" || a.action == "move-rel") {
        cmd.action = ControlAction::StartRelMove;
        cmd.motion = MotionRequest{a.value, a.speed};
    } else if (a.action == "jog-forward") {
        cmd.action = ControlAction::StartJogForward;
    } else if (a.action == "jog-backward") {
        cmd.action = ControlAction::StartJogBackward;
    } else if (a.action == "stop") {
        cmd.action = ControlAction::StopMotion;
    } else {
        std::printf("unhandled action: %s\n", a.action.c_str());
        return 1;
    }

    const std::string opId = svc.submit(std::move(cmd));
    const bool isJog = (a.action == "jog-forward" || a.action == "jog-backward");
    const int rc = driveOperation(svc, opId, a.capMs, a.action.c_str(),
                                  isJog ? a.durationMs : 0);
    printSnapshot(svc);
    return rc;
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
    PlcRuntimeDriverAdapter driver(gateway);
    application_vnext::control::GatewayControlRuntime runtime(gateway);
    MotionControlService svc(driver, runtime);

    for (int i = 0; i < 3 && svc.globallyLocked(); ++i) svc.tick();
    if (svc.globallyLocked()) {
        std::printf("boot/首读未就绪，服务保持全局锁定（连接/拓扑/急停状态异常？）。\n");
    }
    printSnapshot(svc);

    const int rc = run(svc, a);
    client->stop();
    return rc;
}




