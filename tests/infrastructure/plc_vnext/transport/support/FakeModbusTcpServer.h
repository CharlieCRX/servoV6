// ============================================================================
// FakeModbusTcpServer.h —— Step 5 transport: 本地 Modbus TCP 测试服务器（测试替身）
// ============================================================================
// 为 TDD 阶段 1「真实通讯客户端」提供自包含的 Modbus TCP 对端，不依赖外部
// diagslave / 真实 PLC：
//   - 绑定本地随机端口（port=0 自动分配），客户端连接该端口；
//   - 用 map 模拟 PLC 的 0 基址 coils / holding registers；
//   - 解析 MBAP + PDU，按功能码生成正常响应，并真正写回 RAM（写后读可验证）；
//   - 记录收到的请求帧（transactionId / functionCode / 0 基址地址 / 值），
//     供断言「0 基址透传、功能码、地址」；
//   - 可脚本化一次性的故障行为：异常码、无响应（超时）、坏 MBAP、错误 TID、
//     长度不符、响应延迟、响应后断线。
//
// 仅用于测试；生产编译不引入。线程模型：单服务器线程（同步 accept + 同步
// socket I/O），内部互斥锁保护状态。
// ============================================================================
#pragma once

#include <asio.hpp>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace plc_vnext::transport::test_support {

class FakeModbusTcpServer {
public:
    explicit FakeModbusTcpServer(uint8_t unitId = 0x01);
    ~FakeModbusTcpServer();

    // 禁止拷贝（acceptor / thread 不可共享）
    FakeModbusTcpServer(const FakeModbusTcpServer&) = delete;
    FakeModbusTcpServer& operator=(const FakeModbusTcpServer&) = delete;

    /// 启动监听（port=0 表示由系统分配空闲端口），返回实际绑定的端口。
    [[nodiscard]] uint16_t start();
    void stop();
    [[nodiscard]] bool running() const;
    /// 实际绑定的监听端口（start() 之后有效）
    [[nodiscard]] uint16_t port() const { return m_port; }

    // —— PLC RAM 预置 / 读回 ——
    void setCoil(uint16_t address, bool value);
    void setRegister(uint16_t address, uint16_t value);
    [[nodiscard]] std::optional<bool> coil(uint16_t address) const;
    [[nodiscard]] std::optional<uint16_t> reg(uint16_t address) const;

    // —— 一次性脚本化故障（每项被下一次请求消费一次后自动清除） ——
    /// 下一次请求返回 Modbus 异常码（0x01/0x02/0x06/...）
    void scriptException(uint8_t exceptionCode);
    /// 下一次请求收到后不回包（触发客户端读超时）
    void scriptNoResponse();
    /// 下一次响应返回非法 MBAP 头（Protocol ID != 0 且长度异常）
    void scriptBadMbap();
    /// 下一次响应使用错误的 Transaction ID
    void scriptBadTid();
    /// 下一次响应 MBAP length 字段与实际字节数不符（过短）
    void scriptInvalidLength();
    /// 下一次响应前延迟 delayMs 毫秒（配合小 timeoutMs 触发超时）
    void scriptDelayMs(unsigned delayMs);
    /// 下一次响应写回后立即断开连接（模拟链路中断）
    void scriptDisconnectAfterResponse();

    // —— 收到的请求记录（快照，供断言 0 基址透传 / FC / 地址） ——
    struct ReceivedRequest {
        uint16_t transactionId = 0;
        uint8_t functionCode = 0;
        std::vector<uint8_t> data;  // PDU data（不含 FC），0 基址地址 / 值均原样
    };
    [[nodiscard]] std::vector<ReceivedRequest> receivedRequests() const;
    /// 已建立的连接数（含断线后重连）
    [[nodiscard]] unsigned connectionCount() const;

private:
    void run();
    /// 接受并服务一条连接，直到对端关闭或发生错误。
    void serve(asio::ip::tcp::socket socket);
    /// 处理一笔请求帧，返回响应帧；返回空 vector 表示不回包。
    [[nodiscard]] std::vector<uint8_t> handleRequest(const std::vector<uint8_t>& request);

    // —— 响应构建 ——
    [[nodiscard]] std::vector<uint8_t> buildResponse(uint16_t tid, uint8_t fc,
                                                     const std::vector<uint8_t>& data) const;
    [[nodiscard]] std::vector<uint8_t> buildException(uint16_t tid, uint8_t fc,
                                                      uint8_t exCode) const;

    uint8_t m_unitId;
    asio::io_context m_io;
    asio::ip::tcp::acceptor m_acceptor;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    uint16_t m_port = 0;

    /// 当前正在服务的连接 socket；stop() 时关闭以中断阻塞读，保证 join 返回
    std::shared_ptr<asio::ip::tcp::socket> m_activeSocket;

    mutable std::mutex m_mtx;
    std::map<uint16_t, bool> m_coils;
    std::map<uint16_t, uint16_t> m_regs;
    std::vector<ReceivedRequest> m_requests;
    unsigned m_connections = 0;

    // 一次性脚本状态
    std::optional<uint8_t> m_scriptException;
    bool m_scriptNoResponse = false;
    bool m_scriptBadMbap = false;
    bool m_scriptBadTid = false;
    bool m_scriptInvalidLength = false;
    std::optional<unsigned> m_scriptDelayMs;
    bool m_scriptDisconnect = false;
};

}  // namespace plc_vnext::transport::test_support
