// infrastructure/plc/protocol/AsioModbusTcpClient.h
// P4 Phase 2 — 基于 Standalone Asio 的 Modbus TCP 客户端
//
// 职责:
//   - 实现 IModbusClient 接口，提供真实的 Modbus TCP 通讯能力
//   - TCP 连接生命周期管理（连接/断开/重连）
//   - 异步-同步桥接（promise/future），保持 IModbusClient 同步接口
//   - 事务 ID 单调递增，请求-响应配对
//   - 超时控制（steady_timer + socket.cancel()）
//
// 线程模型:
//   - io_context::run() 在独立工作线程中执行
//   - 所有 socket 操作在 io_context 线程中执行（Asio 线程安全要求）
//   - 业务线程通过 promise/future 同步等待结果
//
// 防坑要点（参见 ModbusTCP通讯层实施指导文档 §7）:
//   - 两步分帧读取：先严格读 7 字节 MBAP → 解析 Length → 再严格读 Length 字节 PDU
//   - 超时 ≤ 15ms（适配 PlcPoller 20ms 轮询周期），宁可报废本轮数据也不挂起主循环

#pragma once

#include "infrastructure/ISystemDriver.h"  // CommunicationResult
#include "infrastructure/plc/protocol/IModbusClient.h"

#include <asio.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace plc::protocol {

/**
 * @brief 基于 Standalone Asio 的 Modbus TCP 客户端
 *
 * @details
 * 核心设计:
 *   1. 单 io_context + 单工作线程 驱动所有异步 I/O
 *   2. 每个 Modbus 请求使用 async_write + async_read + steady_timer 超时
 *   3. 连接状态机: Disconnected → Connecting → Connected → Disconnected
 *   4. 事务 ID 递增（Monotonic），用于请求-响应配对
 *
 * 线程模型:
 *   - io_context::run() 在独立线程中执行
 *   - 业务线程通过同步封装接口调用 → 内部用 promise/future 桥接异步操作
 *
 * 推荐初始实现: 同步封装（promise/future），简单可靠
 */
class AsioModbusTcpClient : public IModbusClient {
public:
    /// @brief 连接配置
    struct Config {
        std::string host = "192.168.1.88";   ///< PLC IP 地址
        uint16_t    port = 502;               ///< Modbus TCP 端口
        uint8_t     unitId = 0x01;            ///< Modbus TCP 默认单元 ID
        uint32_t    timeoutMs = 1000;         ///< 读写超时毫秒（生产环境应 ≤ 15ms）
        uint32_t    reconnectIntervalMs = 2000; ///< 断线重连间隔
    };

    /// @brief 构造客户端（不启动连接）
    /// @param config 连接配置
    explicit AsioModbusTcpClient(const Config& config);

    /// @brief 析构 — 停止 io_context 并释放资源
    ~AsioModbusTcpClient() override;

    // 禁止拷贝（socket 资源不可共享）
    AsioModbusTcpClient(const AsioModbusTcpClient&) = delete;
    AsioModbusTcpClient& operator=(const AsioModbusTcpClient&) = delete;

    // ═══════════════════════════════════════
    //  连接管理
    // ═══════════════════════════════════════

    /// @brief 启动 io_context 并异步连接
    ///
    /// 启动后:
    ///   - 创建工作线程执行 io_context::run()
    ///   - 异步尝试连接到配置的主机
    ///   - 连接失败后按 reconnectIntervalMs 间隔自动重连
    ///
    /// @note 可重复调用，已启动时无操作
    void start();

    /// @brief 停止 io_context，断开连接
    ///
    /// 停止后:
    ///   - 关闭 socket
    ///   - 停止工作线程
    ///   - 后续读写操作返回 Disconnected
    ///
    /// @note 可重复调用，已停止时无操作
    void stop();

    /// @brief 当前 TCP 连接状态
    /// @return true 已连接，false 未连接
    ///
    /// 原子操作，可从任意线程安全调用
    [[nodiscard]]
    bool isConnected() const;

    // ═══════════════════════════════════════
    //  IModbusClient 接口实现 (读通道)
    // ═══════════════════════════════════════

    /// @brief FC01 — 读线圈 (Read Coils)
    CommunicationResult readCoils(
        uint16_t startAddress, uint16_t count,
        std::vector<uint8_t>& payload) override;

    /// @brief FC03 — 读保持寄存器 (Read Holding Registers)
    CommunicationResult readHoldingRegisters(
        uint16_t startAddress, uint16_t count,
        std::vector<uint16_t>& payload) override;

    // ═══════════════════════════════════════
    //  IModbusClient 接口实现 (写通道)
    // ═══════════════════════════════════════

    /// @brief FC05 — 写单个线圈 (Write Single Coil)
    CommunicationResult writeSingleCoil(
        uint16_t address, bool value) override;

    /// @brief FC06 — 写单个保持寄存器 (Write Single Holding Register)
    CommunicationResult writeSingleRegister(
        uint16_t address, uint16_t value) override;

    /// @brief FC10 — 写多个保持寄存器 (Write Multiple Holding Registers)
    CommunicationResult writeMultipleRegisters(
        uint16_t startAddress,
        const std::vector<uint16_t>& values) override;

private:
    // ═══════════════════════════════════════
    //  内部实现
    // ═══════════════════════════════════════

    /// @brief 执行一次完整的 Modbus 请求-响应周期（同步阻塞）
    /// @param frame 完整的请求帧
    /// @param[out] response 响应帧
    /// @return 通讯结果
    CommunicationResult executeTransaction(
        const std::vector<uint8_t>& frame,
        std::vector<uint8_t>& response);

    /// @brief 获取下一个事务 ID（线程安全，单调递增）
    /// @return 事务 ID [0, 65535]
    uint16_t nextTransactionId();

    /// @brief 设置 socket 选项（超时、nodelay 等）
    void configureSocket();

    /// @brief 启动异步重连流程（DNS 解析 + TCP 连接）
    void startReconnect();

    /// @brief 安排延迟重连（由 startReconnect 的回调调用）
    void scheduleReconnect();

    /// @brief 内部状态清理（停止时调用）
    void cleanup();

    // ═══════════════════════════════════════
    //  Asio 基础设施
    // ═══════════════════════════════════════

    Config                          m_config;
    asio::io_context                m_ioctx;
    std::unique_ptr<asio::io_context::work> m_work;  // 防止 io_context 提前退出
    std::thread                     m_worker;
    asio::ip::tcp::socket           m_socket;
    asio::steady_timer              m_timer;          // 超时/重连定时器

    /// @brief 事务 ID 计数器（原子，Monotonic）
    std::atomic<uint16_t>           m_transactionId{0};

    /// @brief 连接状态标志（原子，业务线程可无锁查询）
    std::atomic<bool>               m_connected{false};

    /// @brief 运行状态标志（原子）
    std::atomic<bool>               m_running{false};

    /// @brief socket 操作互斥锁（串行化 socket 操作，防止并发写导致帧交织）
    mutable std::mutex              m_socketMutex;
};

} // namespace plc::protocol
