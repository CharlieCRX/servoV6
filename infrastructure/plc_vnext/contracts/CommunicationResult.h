// ============================================================================
// CommunicationResult.h —— Step 1 contracts: 通讯结果
// ============================================================================
// 语义与旧 infrastructure/ISystemDriver.h 的 CommunicationResult 逐字段一致
// （七态 Status、exceptionCode、diagnostic、ok/retryable/isNetworkIssue/
//  isProtocolIssue/isDisconnected），但独立定义、零 include 依赖。
// 这是 transport 与 ISystemDriver 解耦的唯一路径。
//
// 本类型只表达"命令是否成功写入 PLC 的寄存器"，不表达 PLC 执行结果、
// 不表达物理动作状态（那是 pollFeedback 的职责）。
// ============================================================================
#pragma once

#include <string>

namespace plc_vnext::contracts {

struct CommunicationResult {
    enum class Status {
        /// 成功写入 PLC 寄存器（收到正常 Modbus 响应，无异常码）
        Sent,

        /// TCP 连接失败 / 网线断开 / PLC 掉电 / Socket 错误（不可重试）
        NetworkError,

        /// 通讯超时（可重试；超时不等于断线，之后网络可能立即恢复）
        Timeout,

        /// PLC 忙（Modbus Exception 0x06 -- Server Device Busy，可重试）
        Busy,

        /// Modbus 异常响应（非 Busy 的其他 Exception Code，保留 exceptionCode）
        ProtocolError,

        /// 返回数据非法（CRC 通过但格式/语义不符，不可重试）
        InvalidResponse,

        /// 当前未连接（已知未连接状态，与 NetworkError 的意外断连区分）
        Disconnected
    };

    Status status = Status::Sent;
    int exceptionCode = 0;  ///< 仅 ProtocolError 时有效（0x01/0x02/0x03/...）
    std::string diagnostic; ///< 仅用于日志/UI，不参与控制流

    [[nodiscard]] bool ok() const { return status == Status::Sent; }

    /// 是否可重试：Timeout / Busy
    [[nodiscard]] bool retryable() const {
        return status == Status::Timeout || status == Status::Busy;
    }

    /// 是否网络相关问题：NetworkError / Timeout / Disconnected
    [[nodiscard]] bool isNetworkIssue() const {
        return status == Status::NetworkError || status == Status::Timeout
            || status == Status::Disconnected;
    }

    /// 是否 Modbus 协议层问题（不含 Busy）
    [[nodiscard]] bool isProtocolIssue() const {
        return status == Status::ProtocolError;
    }

    /// 是否已断连
    [[nodiscard]] bool isDisconnected() const {
        return status == Status::Disconnected;
    }

    static CommunicationResult sent() { return {}; }
    static CommunicationResult disconnected(const std::string& msg) {
        return {Status::Disconnected, 0, msg};
    }
};

}  // namespace plc_vnext::contracts
