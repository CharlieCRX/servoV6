// ============================================================================
// AsioModbusTcpClient.h —— Step 5 transport: 真实 Modbus TCP 客户端
// ============================================================================
// 迁移自旧 infrastructure/plc/protocol/AsioModbusTcpClient，适配 vnext 接口：
//   - 实现 transport::IModbusClient（窄接口），返回值用 contracts::CommunicationResult；
//   - 真实 Asio (Standalone) socket，单 io_context + 单工作线程 + promise/future
//     同步桥接，事务 ID 单调递增，steady_timer 超时 + socket.cancel；
//   - TCP 连接 / 断开 / 自动重连（requestReconnect 立即重连）；
//   - 两步分帧读取（先 7 字节 MBAP -> 再 Length 字节 PDU），MBAP/PDU 长度校验、
//     Transaction ID 校验、Modbus 异常响应转换；
//   - 地址一律 0 基址原样透传（不做 +1 / 40001 偏移，Modbus 偏移由 layout 层负责）；
//   - 断线/超时绝不自动重放运动命令（只完成当前一笔事务，不缓存命令）。
//
// 线程模型（与旧实现一致，已修复悬空引用问题）：
//   - io_context::run() 在独立工作线程执行，所有 socket 操作在该线程串行；
//   - 业务线程经 promise/future 同步等待结果；m_socketMutex 串行化多线程事务。
//
// 日志：复用旧 infrastructure/logger/Logger.h（按决策引入依赖）。
// ============================================================================
#pragma once

#include <asio.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/transport/IModbusClient.h"

namespace plc_vnext::transport {

/// @brief 基于 Standalone Asio 的真实 Modbus TCP 客户端。
/// 迁移完成前旧桩已被本实现替换；编译进 plc_vnext 静态库。
class AsioModbusTcpClient final : public IModbusClient {
public:
    /// @brief 连接配置（IP/端口/Unit ID/超时/重连间隔均可配置）
    struct Config {
        std::string host = "127.0.0.1";   ///< PLC IP 地址
        uint16_t    port = 502;           ///< Modbus TCP 端口
        uint8_t     unitId = 0x01;        ///< Modbus TCP Unit ID
        uint32_t    timeoutMs = 1000;     ///< 读写超时（毫秒）
        uint32_t    reconnectIntervalMs = 2000;  ///< 断线自动重连间隔（毫秒）
    };

    /// @brief 构造客户端（不启动连接）
    explicit AsioModbusTcpClient(const Config& config);

    /// @brief 析构 —— 停止 io_context 并释放资源
    ~AsioModbusTcpClient() override;

    AsioModbusTcpClient(const AsioModbusTcpClient&) = delete;
    AsioModbusTcpClient& operator=(const AsioModbusTcpClient&) = delete;

    // ═══════════════════════════════════════════
    //  连接生命周期（非接口成员，生产入口调用）
    // ═══════════════════════════════════════════

    /// @brief 启动 io_context + 工作线程并异步连接；可重复调用，已启动时无操作
    void start();

    /// @brief 停止 io_context、断开连接；可重复调用，已停止时无操作
    void stop();

    // ═══════════════════════════════════════════
    //  IModbusClient —— 连接管理
    // ═══════════════════════════════════════════

    [[nodiscard]] bool isConnected() const override;

    /// @brief 请求立即重连（跳过自动重连等待间隔）
    void requestReconnect() override;

    // ═══════════════════════════════════════════
    //  IModbusClient —— 读通道（0 基址）
    // ═══════════════════════════════════════════

    contracts::CommunicationResult readCoils(uint16_t startAddress, uint16_t count,
                                             std::vector<uint8_t>& payload) override;
    contracts::CommunicationResult readHoldingRegisters(
        uint16_t startAddress, uint16_t count, std::vector<uint16_t>& payload) override;

    // ═══════════════════════════════════════════
    //  IModbusClient —— 写通道（0 基址）
    // ═══════════════════════════════════════════

    contracts::CommunicationResult writeSingleCoil(uint16_t address, bool value) override;
    contracts::CommunicationResult writeSingleRegister(uint16_t address,
                                                       uint16_t value) override;
    contracts::CommunicationResult writeMultipleRegisters(
        uint16_t startAddress, const std::vector<uint16_t>& values) override;

private:
    /// @brief 执行一次完整 Modbus 请求-响应周期（同步阻塞，含超时）
    contracts::CommunicationResult executeTransaction(
        const std::vector<uint8_t>& frame, std::vector<uint8_t>& response);

    /// @brief 获取下一个事务 ID（线程安全，单调递增，uint16 回绕）
    uint16_t nextTransactionId();

    /// @brief 设置 socket 选项（no_delay / keep_alive / 平台级收发超时）
    void configureSocket();

    /// @brief 启动异步重连（DNS 解析 + TCP 连接）
    void startReconnect();

    /// @brief 安排延迟重连
    void scheduleReconnect();

    /// @brief 构建完整 Modbus TCP 请求帧（MBAP + PDU），0 基址原样
    std::vector<uint8_t> buildFrame(uint16_t tid, uint8_t fc,
                                    const std::vector<uint8_t>& pdu);
    /// @brief 从 socket 读响应：两步分帧 + MBAP/PDU/长度/TID 校验 + 异常转换
    /// @param requestTid 请求事务 ID；@param requestFc 请求功能码
    /// @param requestAddr 请求 0 基址起始地址（用于诊断）
    /// @param timeout 超时毫秒；@param[out] response 完整响应帧（MBAP+PDU）
    contracts::CommunicationResult readFullResponse(uint16_t requestTid, uint8_t requestFc,
                                                    uint16_t requestAddr, uint16_t timeout,
                                                    std::vector<uint8_t>& response);

    /// @brief 诊断日志（endpoint + 功能码 + 地址 + 错误类型），可追踪
    void diag(const std::string& level, const std::string& msg) const;

    Config m_config;
    asio::io_context m_ioctx;
    std::unique_ptr<asio::io_context::work> m_workGuard;
    std::thread m_worker;
    asio::ip::tcp::socket m_socket;
    asio::steady_timer m_timer;

    std::atomic<uint16_t> m_transactionId{0};
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_running{false};
    mutable std::mutex m_socketMutex;

    std::string m_moduleName;  ///< 形如 "ModbusTCP|host:port"，日志多实例区分
};

}  // namespace plc_vnext::transport
