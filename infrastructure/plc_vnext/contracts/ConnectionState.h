// ============================================================================
// ConnectionState.h —— Step 10 contracts: 连接状态快照
// ============================================================================
// 语义与旧 infrastructure/ISystemDriver.h 的 ConnectionState 逐字段一致
// （bool connected + std::string diagnostic），但零 include 依赖、独立定义，
// 供 transport 层与上层（application/presentation）经 IPlcRuntimeGateway 解耦。
// 本类型只表达"TCP 是否已连接 + 诊断文本"，不做任何业务/协议判定。
// ============================================================================
#pragma once

#include <string>
#include <utility>

namespace plc_vnext::contracts {

struct ConnectionState {
    /// 当前 TCP 是否已连接。
    bool connected = false;
    /// 诊断文本，如 "已连接 192.168.1.88:502" 或 "断连: ECONNREFUSED"。
    /// 仅用于日志/UI，不参与控制流。
    std::string diagnostic;

    static ConnectionState connectedState(std::string diagnostic = {}) {
        ConnectionState s;
        s.connected = true;
        s.diagnostic = std::move(diagnostic);
        return s;
    }
    static ConnectionState disconnectedState(std::string diagnostic = {}) {
        ConnectionState s;
        s.connected = false;
        s.diagnostic = std::move(diagnostic);
        return s;
    }
};

}  // namespace plc_vnext::contracts
