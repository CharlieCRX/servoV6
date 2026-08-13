// ============================================================================
// plc_vnext_readonly_probe.cpp —— 基于新 C++ 客户端 + PlcRuntimeGateway 的真实
// PLC 只读探针（阶段 1 补验 / 阶段 2 最小落地入口）
// ============================================================================
// 目的：用与正式链路相同的对象组装（AsioModbusTcpClient → PlcRuntimeGateway →
// readTopology/readRuntime）连接真实 PLC，**只读**输出，并与
// tools/plc_read_validate.py 结果对拍。证明：
//   - 不仅 Python 原生 socket 能读 PLC，plc_vnext::AsioModbusTcpClient 与
//     PlcRuntimeGateway 也能在真实 PLC 上正确读取。
//
// 只读约束（禁止）：
//   - 不调用 writeAxis() / submitGantryRequest()；
//   - 不调用任何 FC05/FC06/FC10 写；
//   - 不接入 UI / UDP / 摇杆。
// M224/M225 经同一 AsioModbusTcpClient 用 readCoils(224,2) 只读（方案 §4.4）。
//
// 用法：
//   plc_vnext_readonly_probe [--host IP] [--port 502] [--unit 1]
//       [--timeout 1000] [--reconnect 2000] [--poll N] [--interval MS]
//       [--wait-ms 3000] [--json]
// ============================================================================
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "infrastructure/logger/Logger.h"
#include "infrastructure/plc_vnext/PlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/SnapshotQuality.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"
#include "infrastructure/plc_vnext/transport/AsioModbusTcpClient.h"

namespace {

using plc_vnext::contracts::ReadResult;
using plc_vnext::contracts::RuntimeSnapshot;
using plc_vnext::contracts::TopologySnapshot;
using plc_vnext::transport::AsioModbusTcpClient;

struct Args {
    std::string host = "192.168.1.88";
    uint16_t port = 502;
    uint8_t unit = 0x01;
    uint32_t timeoutMs = 1000;
    uint32_t reconnectMs = 2000;
    unsigned poll = 1;          // 轮询次数
    unsigned intervalMs = 500;  // 轮询间隔
    unsigned waitMs = 3000;     // 等待首次连接上限
    bool json = false;
};

int usage(const char* prog) {
    std::printf(
        "usage: %s [--host IP] [--port 502] [--unit 1] [--timeout 1000]\n"
        "          [--reconnect 2000] [--poll N] [--interval MS] [--wait-ms 3000] [--json]\n"
        "只读探针：readTopology() + readRuntime() + M224/M225 + 连接状态。禁止任何写。\n",
        prog);
    return 0;
}

// 简易命令行解析；未知参数返回 false
bool parseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> std::optional<std::string> {
            if (i + 1 < argc) return std::string(argv[++i]);
            return std::nullopt;
        };
        if (std::strcmp(argv[i], "--host") == 0) {
            auto v = next();
            if (!v) return false;
            a.host = *v;
        } else if (std::strcmp(argv[i], "--port") == 0) {
            auto v = next();
            if (!v) return false;
            a.port = static_cast<uint16_t>(std::stoi(*v));
        } else if (std::strcmp(argv[i], "--unit") == 0) {
            auto v = next();
            if (!v) return false;
            a.unit = static_cast<uint8_t>(std::stoi(*v));
        } else if (std::strcmp(argv[i], "--timeout") == 0) {
            auto v = next();
            if (!v) return false;
            a.timeoutMs = static_cast<uint32_t>(std::stoul(*v));
        } else if (std::strcmp(argv[i], "--reconnect") == 0) {
            auto v = next();
            if (!v) return false;
            a.reconnectMs = static_cast<uint32_t>(std::stoul(*v));
        } else if (std::strcmp(argv[i], "--poll") == 0) {
            auto v = next();
            if (!v) return false;
            a.poll = static_cast<unsigned>(std::stoul(*v));
        } else if (std::strcmp(argv[i], "--interval") == 0) {
            auto v = next();
            if (!v) return false;
            a.intervalMs = static_cast<unsigned>(std::stoul(*v));
        } else if (std::strcmp(argv[i], "--wait-ms") == 0) {
            auto v = next();
            if (!v) return false;
            a.waitMs = static_cast<unsigned>(std::stoul(*v));
        } else if (std::strcmp(argv[i], "--json") == 0) {
            a.json = true;
        } else if (std::strcmp(argv[i], "-h") == 0 ||
                   std::strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return false;  // 已打印 usage，调用方直接退出
        } else {
            std::printf("unknown option: %s\n", argv[i]);
            return false;
        }
    }
    return true;
}

bool waitConnected(AsioModbusTcpClient& client, unsigned waitMs) {
    auto t0 = std::chrono::steady_clock::now();
    while (!client.isConnected()) {
        if (std::chrono::steady_clock::now() - t0 >
            std::chrono::milliseconds(waitMs))
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

// —— 与 tools/plc_read_validate.py 一致的枚举文本 ——
const char* motionStateText(int v) {
    static const char* t[] = {"轴控入口未使能", "空闲", "正向点动", "反向点动",
                              "绝对定位", "相对定位"};
    return (v >= 0 && v < 6) ? t[v] : "?";
}
const char* motionLimitText(int v) {
    static const char* t[] = {"无限位", "正软限位", "负软限位", "正硬限位", "负硬限位"};
    return (v >= 0 && v < 5) ? t[v] : "?";
}
const char* gantryStateText(int v) {
    static const char* t[] = {"未配置", "已解除", "建立中", "已联动", "解除中", "故障"};
    return (v >= 0 && v < 6) ? t[v] : "?";
}
const char* commandResultText(int v) {
    static const char* t[] = {"无结果", "处理中", "成功", "拒绝", "失败"};
    return (v >= 0 && v < 5) ? t[v] : "?";
}
const char* qualityText(plc_vnext::contracts::SnapshotQuality q) {
    switch (q) {
        case plc_vnext::contracts::SnapshotQuality::Trusted:
            return "Trusted";
        case plc_vnext::contracts::SnapshotQuality::Stale:
            return "Stale";
        case plc_vnext::contracts::SnapshotQuality::Partial:
            return "Partial";
        case plc_vnext::contracts::SnapshotQuality::TransportFailed:
            return "TransportFailed";
    }
    return "?";
}
using TopoResult = plc_vnext::contracts::ReadResult<plc_vnext::contracts::TopologySnapshot>;
const char* failureKindText(typename TopoResult::FailureKind k) {
    switch (k) {
        case TopoResult::FailureKind::Transport: return "Transport";
        case TopoResult::FailureKind::Decode: return "Decode";
        case TopoResult::FailureKind::RevisionChanged: return "RevisionChanged";
    }
    return "?";
}

void printTopology(const TopoResult& r, bool json) {
    if (!r.hasValue()) {
        if (json) {
            printf("  \"topology\": {\"ok\":false,\"kind\":\"%s\",\"diagnostic\":\"%s\"},\n",
                   failureKindText(r.failureKind()), r.diagnostic().c_str());
        } else {
            printf("[topology] ok=false kind=%s diagnostic=%s\n",
                   failureKindText(r.failureKind()), r.diagnostic().c_str());
        }
        return;
    }
    const auto& t = r.value();
    const auto& h = t.header;
    if (json) {
        printf("  \"topology\": {\n    \"ok\":true,\n"
               "    \"header\": {\"magic\":%d,\"schemaVersion\":%d,\"revision\":%d,"
               "\"configValid\":%s,\"configErrorCode\":%d},\n    \"groups\": [",
               h.magic, h.schemaVersion, h.revision,
               h.configValid ? "true" : "false", h.configErrorCode);
        for (size_t g = 0; g < t.groups.size(); ++g) {
            const auto& gr = t.groups[g];
            printf("\n      {\"index\":%zu,\"groupCode\":%d,\"valid\":%s,\"roles\":[",
                   g, gr.groupCode, gr.valid ? "true" : "false");
            for (size_t i = 0; i < gr.roles.size(); ++i) {
                const auto& role = gr.roles[i];
                printf("{\"valid\":%s,\"plcAxisIndex\":%d,\"motionMode\":%d,\"axisClass\":%d}",
                       role.valid ? "true" : "false", role.plcAxisIndex,
                       role.motionMode, role.axisClass);
                if (i + 1 < gr.roles.size()) printf(",");
            }
            printf("]}");
            if (g + 1 < t.groups.size()) printf(",");
        }
        printf("\n    ]},\n");
    } else {
        printf("[topology] ok=true\n");
        printf("  header: magic=0x%08X schemaVersion=%d revision=%d configValid=%d configErrorCode=%d\n",
               static_cast<unsigned>(h.magic), h.schemaVersion, h.revision,
               h.configValid ? 1 : 0, h.configErrorCode);
        for (size_t g = 0; g < t.groups.size(); ++g) {
            const auto& gr = t.groups[g];
            printf("  group[%zu]: groupCode=%d (%s) valid=%d hmiVisible=%d\n",
                   g, gr.groupCode, gr.groupCode == 0 ? "A" : "B",
                   gr.valid ? 1 : 0, gr.hmiVisible ? 1 : 0);
            for (size_t i = 0; i < gr.roles.size(); ++i) {
                const auto& role = gr.roles[i];
                printf("    role[%zu]: valid=%d plcAxisIndex=%d motorNo=%d motionMode=%d axisClass=%d\n",
                       i, role.valid ? 1 : 0, role.plcAxisIndex, role.motorNo,
                       role.motionMode, role.axisClass);
            }
        }
    }
}
void printRuntime(const ReadResult<RuntimeSnapshot>& r, bool json) {
    using R = plc_vnext::contracts::ReadResult<RuntimeSnapshot>;
    if (!r.hasValue()) {
        if (json) {
            printf("  \"runtime\": {\"ok\":false,\"kind\":\"%s\",\"diagnostic\":\"%s\"},\n",
                   r.failureKind() == R::FailureKind::Transport ? "Transport"
                       : r.failureKind() == R::FailureKind::Decode ? "Decode"
                       : "RevisionChanged",
                   r.diagnostic().c_str());
        } else {
            printf("[runtime] ok=false kind=%s diagnostic=%s\n",
                   r.failureKind() == R::FailureKind::Transport ? "Transport"
                       : r.failureKind() == R::FailureKind::Decode ? "Decode"
                       : "RevisionChanged",
                   r.diagnostic().c_str());
        }
        return;
    }
    const auto& s = r.value();
    if (json) {
        printf("  \"runtime\": {\"ok\":true,\"quality\":\"%s\",\"sampledAtMs\":%lld,"
               "\"durationMs\":%lld,\"axes\":[",
               qualityText(s.quality),
               static_cast<long long>(s.sampledAtMs), static_cast<long long>(s.durationMs));
        for (size_t i = 0; i < s.axes.size(); ++i) {
            const auto& ax = s.axes[i];
            printf("{\"slot\":%d,\"trusted\":%s,\"motionState\":%d,\"motionLimit\":%d,"
                   "\"alarmWord\":%u,\"absPosition\":%.4f,\"relPosition\":%.4f,"
                   "\"manualSpeed\":%.4f,\"positioningSpeed\":%.4f}",
                   ax.slot, ax.trusted ? "true" : "false", ax.motionState,
                   ax.motionLimit, static_cast<unsigned>(ax.alarmWord), ax.absPosition,
                   ax.relPosition, ax.manualSpeed, ax.positioningSpeed);
            if (i + 1 < s.axes.size()) printf(",");
        }
        printf("],\"gantry\":[");
        for (size_t g = 0; g < s.gantry.size(); ++g) {
            const auto& gn = s.gantry[g];
            printf("{\"index\":%zu,\"trusted\":%s,\"state\":%d,\"ackSeq\":%d,"
                   "\"commandResult\":%d,\"commandErrorCode\":%d,"
                   "\"memberControlAllowed\":%s,\"logicalControlAllowed\":%s,"
                   "\"x1InGear\":%s,\"x2InGear\":%s,\"fault\":%s}",
                   g, gn.trusted ? "true" : "false", gn.state, gn.ackSeq,
                   gn.commandResult, gn.commandErrorCode,
                   gn.memberControlAllowed ? "true" : "false",
                   gn.logicalControlAllowed ? "true" : "false",
                   gn.x1InGear ? "true" : "false", gn.x2InGear ? "true" : "false",
                   gn.fault ? "true" : "false");
            if (g + 1 < s.gantry.size()) printf(",");
        }
        printf("]},\n");
    } else {
        printf("[runtime] ok=true quality=%s sampledAtMs=%lld durationMs=%lld\n",
               qualityText(s.quality),
               static_cast<long long>(s.sampledAtMs), static_cast<long long>(s.durationMs));
        for (size_t i = 0; i < s.axes.size(); ++i) {
            const auto& ax = s.axes[i];
            printf("  axis[%zu]: slot=%d trusted=%d motionState=%d(%s) motionLimit=%d(%s) "
                   "alarmWord=0x%04X abs=%.4f rel=%.4f manual=%.4f posSpeed=%.4f\n",
                   i, ax.slot, ax.trusted ? 1 : 0, ax.motionState, motionStateText(ax.motionState),
                   ax.motionLimit, motionLimitText(ax.motionLimit),
                   static_cast<unsigned>(ax.alarmWord), ax.absPosition, ax.relPosition,
                   ax.manualSpeed, ax.positioningSpeed);
        }
        for (size_t g = 0; g < s.gantry.size(); ++g) {
            const auto& gn = s.gantry[g];
            printf("  gantry[%zu]: trusted=%d state=%d(%s) ackSeq=%d commandResult=%d(%s) "
                   "cmdErr=%d memberCtrl=%d logicalCtrl=%d x1Gear=%d x2Gear=%d fault=%d\n",
                   g, gn.trusted ? 1 : 0, gn.state, gantryStateText(gn.state), gn.ackSeq,
                   gn.commandResult, commandResultText(gn.commandResult),
                   gn.commandErrorCode, gn.memberControlAllowed ? 1 : 0,
                   gn.logicalControlAllowed ? 1 : 0, gn.x1InGear ? 1 : 0,
                   gn.x2InGear ? 1 : 0, gn.fault ? 1 : 0);
        }
    }
}

// M224/M225 只读：经同一 client（同一串行通道）读取，方案 §4.4。
void printSafety(AsioModbusTcpClient& client, bool json) {
    std::vector<uint8_t> bits;
    auto res = client.readCoils(224, 2, bits);
    bool ok = res.ok() && bits.size() >= 1;
    bool m224 = ok && ((bits[0] & 0x01) != 0);
    bool m225 = ok && ((bits[0] & 0x02) != 0);
    if (json) {
        printf("  \"safety\": {\"ok\":%s,\"M224_emergencyStop\":%s,\"M225_release\":%s},\n",
               ok ? "true" : "false", m224 ? "true" : "false", m225 ? "true" : "false");
    } else {
        printf("[safety] M224(emergencyStop)=%d M225(release)=%d (read=%s %s)\n",
               m224 ? 1 : 0, m225 ? 1 : 0, ok ? "ok" : "fail",
               ok ? "" : res.diagnostic.c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    // 探针是独立只读工具，自行用 printf 输出；禁用 Logger 以免其 TLS/后台线程
    // 影响工具生命周期（探针不需要 Logger 的日志流）。
    LoggerConfig lc;
    lc.enableConsole = false;
    lc.enableFile = false;
    Logger::init(lc);
    // 保证 main 任何返回路径前 join Logger 后台线程（否则退出时 std::terminate）
    struct LoggerShutdownGuard {
        ~LoggerShutdownGuard() { Logger::shutdown(); }
    } loggerGuard;

    Args a;
    if (argc > 1) {
        if (!parseArgs(argc, argv, a)) return 1;  // parseArgs 已打印 usage / 未知参数
    }

    AsioModbusTcpClient::Config cfg;
    cfg.host = a.host;
    cfg.port = a.port;
    cfg.unitId = a.unit;
    cfg.timeoutMs = a.timeoutMs;
    cfg.reconnectIntervalMs = a.reconnectMs;

    auto client = std::make_shared<AsioModbusTcpClient>(cfg);
    client->start();

    if (!waitConnected(*client, a.waitMs)) {
        std::printf("连接失败/超时：%s:%u（%ums 内未建立 TCP 连接）。"
                    "请确认 PLC 可达且端口/UnitID 正确。\n",
                    a.host.c_str(), a.port, a.waitMs);
        client->stop();
        return 2;
    }

    // 只读 Gateway：探针不调用 writeAxis / submitGantryRequest（禁止写）
    plc_vnext::PlcRuntimeGateway gateway(client);

    bool first = true;
    for (unsigned i = 0; i < a.poll; ++i) {
        if (!first) std::this_thread::sleep_for(std::chrono::milliseconds(a.intervalMs));
        first = false;

        auto cs = gateway.connectionState();
        if (a.json) {
            if (i == 0) printf("{\n");
            printf("  \"poll\":%u,\"connection\":{\"connected\":%s},\n",
                   i, cs.connected ? "true" : "false");
        } else {
            std::printf("[poll %u] connection connected=%d\n", i, cs.connected ? 1 : 0);
        }

        printTopology(gateway.readTopology(), a.json);
        printRuntime(gateway.readRuntime(), a.json);
        printSafety(*client, a.json);

        if (a.json && i + 1 == a.poll) printf("}\n");
    }

    client->stop();
    return 0;
}


