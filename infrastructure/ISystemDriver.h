#pragma once

#include "domain/command/SystemCommand.h"
#include <string>

class SystemContext;  // 前向声明，避免循环依赖（SystemContext.h 已 include 本文件）

/**
 * @brief 通讯层结果 — 精确表达 Modbus TCP 通讯的每一类失败
 *
 * 设计原则:
 *   1. 不表达 PLC 执行结果 — 那是 pollFeedback 的职责
 *   2. 不表达物理动作状态 — 那是 pollFeedback 的职责
 *   3. 只表达"命令是否成功写入 PLC 的寄存器"
 *
 * 错误层级（从低到高）:
 *   L1 — 网络层:    NetworkError   (socket 断连/拒绝，不可重试)
 *   L2 — 传输层:    Timeout        (超时，可重试)
 *   L3 — Modbus层:  ProtocolError  (异常响应，保留 exceptionCode)
 *                    Busy           (PLC忙，可重试)
 *   L4 — 数据层:    InvalidResponse (数据非法，不可重试)
 *   L0 — 会话层:    Disconnected   (当前未连接，不可重试)
 */
struct CommunicationResult {

    enum class Status {

        /// 成功写入 PLC 寄存器（收到正常 Modbus 响应，无异常码）
        Sent,

        /// TCP 连接失败 / 网线断开 / PLC 掉电 / Socket 错误
        /// 包括: ECONNRESET, ECONNREFUSED, EHOSTUNREACH, ENETUNREACH
        NetworkError,

        /// 通讯超时（Socket write/read 超时，交换机抖动，PLC 扫描周期过长）
        /// 注意: 超时不等于断线，超时后网络可能立即恢复
        Timeout,

        /// PLC 忙（Modbus Exception Code 0x06 — Server Device Busy）
        /// 注意: 这是可重试的——稍等 PLC 空闲后重发
        Busy,

        /// Modbus 异常响应（非 Busy 的其他 Exception Code）
        /// 如: 0x01 Illegal Function, 0x02 Illegal Address, 0x03 Illegal Value
        /// 保留 exceptionCode 供诊断
        ProtocolError,

        /// 返回数据非法（CRC 通过但数据格式/语义不符合预期）
        /// 如: 响应长度错误、寄存器值超出物理范围
        InvalidResponse,

        /// 当前未连接（驱动处于未初始化/已断开状态）
        /// 与 NetworkError 的区别: Disconnected 是已知的未连接状态，
        /// NetworkError 是通信尝试中发生的意外断连
        Disconnected
    };

    Status status = Status::Sent;

    /// Modbus Exception Code（仅在 ProtocolError 时有效）
    /// 0x01 = Illegal Function
    /// 0x02 = Illegal Address
    /// 0x03 = Illegal Value
    /// 0x06 = Device Busy（此时 status 为 Busy 而非 ProtocolError）
    int exceptionCode = 0;

    /// 诊断信息（用于日志/UI，不参与控制流）
    /// 例: "192.168.1.100:502 connect failed: ECONNREFUSED"
    std::string diagnostic;

    // ==================== 便捷判断方法 ====================

    [[nodiscard]]
    bool ok() const {
        return status == Status::Sent;
    }

    /// @brief 是否可重试（工业现场决策核心）
    ///
    /// 可重试:
    ///   - Timeout: PLC 可能只是忙，稍后重试
    ///   - Busy:    PLC 显式告知忙，等待后重试
    ///
    /// 不可重试:
    ///   - NetworkError:    网线断开/掉电，重试无意义
    ///   - ProtocolError:   编程/配置错误，重试同样会失败
    ///   - InvalidResponse: 数据格式问题，重试无意义
    ///   - Disconnected:    未连接，需先建立连接
    ///
    [[nodiscard]]
    bool retryable() const {
        switch (status) {
            case Status::Timeout:
            case Status::Busy:
                return true;
            default:
                return false;
        }
    }

    /// @brief 是否为网络相关问题（用于统一处理网络层）
    [[nodiscard]]
    bool isNetworkIssue() const {
        switch (status) {
            case Status::NetworkError:
            case Status::Timeout:
            case Status::Disconnected:
                return true;
            default:
                return false;
        }
    }

    /// @brief 是否为 Modbus 协议层问题（不含 Busy）
    [[nodiscard]]
    bool isProtocolIssue() const {
        return status == Status::ProtocolError;
    }
};

/**
 * @brief 工业控制系统驱动的统一接口
 *
 * 设计原则:
 *   1. Command / Feedback 双通路: send() 发命令，pollFeedback() 收反馈。
 *   2. send() 返回通讯结果 — 只表达"帧是否送达"，不表达"PLC 是否执行"。
 *   3. pollFeedback() 是主动拉取 — 负责物理状态回传，每主循环周期调用一次。
 */
class ISystemDriver {
public:
    virtual ~ISystemDriver() = default;

    // ===== 命令通路 =====

    /// @brief 向硬件发送一个统一命令
    /// @return CommunicationResult — 只表达通讯帧是否成功送达 PLC
    virtual CommunicationResult send(const SystemCommand& cmd) = 0;

    // ===== 反馈通路 =====

    /// @brief 从硬件拉取反馈并分发给 SystemContext 内的所有领域实体
    ///
    /// 每主循环周期调用一次（典型周期: 10ms）
    ///
    /// 内部执行:
    ///   1. Read:  从硬件读取当前状态
    ///   2. Translate: 硬件数据 → 领域反馈结构体
    ///   3. Dispatch: 注入 axis->applyFeedback() / emergencyStopController.applyFeedback() / ...
    ///
    /// 失败策略: 通信失败时保留上次已知反馈值，不更新，记 TRACE_WARN
    ///
    /// @param ctx 目标分组上下文
    virtual void pollFeedback(SystemContext& ctx) = 0;
};
