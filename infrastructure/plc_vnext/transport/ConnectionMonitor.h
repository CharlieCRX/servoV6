// ============================================================================
// ConnectionMonitor.h —— Step 5 transport: 连接监控（断连/退避/手动重连）
// ============================================================================
// 职责：观察每次 I/O 事务的通讯结果，据此维护连接状态、连续失败计数与退避
// 时长；手动重连 requestReconnect() 委托给底层 IModbusClient，不自行开 socket。
//
// 安全不变量（★ 禁止违反）：
//   - 本类只读、只观察、只委托，绝不发出任何 Modbus 写/读命令；
//   - 断线/重连过程中绝不"重放"任何命令（连接层不得保留可重放的运动命令）；
//   - 这里不出现槽位/轴/联动语义（transport 边界）。
//
// 本类暴露的状态用简单布尔/计数表达（不含 contracts::ConnectionState 的完整
// DTO；后者按需在 Gateway 层装配）。
// ============================================================================
#pragma once

#include <memory>
#include <mutex>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/transport/IModbusClient.h"

namespace plc_vnext::transport {

class ConnectionMonitor {
public:
    explicit ConnectionMonitor(IModbusClientPtr client);

    // —— 观察点：每次事务结束后调用一次 ——
    void onTransactionResult(const contracts::CommunicationResult& result);

    // —— 与底层同步连接状态（连接/断线由客户端驱动时调用） ——
    void refresh();

    // —— 手动重连：委托底层客户端 ——
    void requestReconnect();

    // —— 只读查询（线程安全） ——
    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] unsigned consecutiveFailures() const;
    [[nodiscard]] bool isBackoffActive() const;
    [[nodiscard]] unsigned backoffMs() const;

private:
    unsigned computeBackoff(unsigned failures) const;

    IModbusClientPtr m_client;
    mutable std::mutex m_mtx;

    bool m_connected = false;
    unsigned m_consecutiveFailures = 0;
    unsigned m_backoffMs = 0;

    // 退避配置：失败一次即进入退避，按 2 倍递增，封顶 8s。
    static constexpr unsigned kBaseBackoffMs = 500u;
    static constexpr unsigned kMaxBackoffMs = 8000u;
};

}  // namespace plc_vnext::transport
