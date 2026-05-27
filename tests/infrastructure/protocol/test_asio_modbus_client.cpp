// tests/infrastructure/protocol/test_asio_modbus_client.cpp
// P4 Phase 2 — AsioModbusTcpClient 连接管理测试 (TDD)
//
// 测试范围:
//   - 构造/析构/start/stop 生命周期
//   - 连接状态机: Disconnected → Connecting → Connected → Disconnected
//   - 无效主机连接返回 NetworkError
//   - 重连机制基础验证
//   - isConnected() 状态正确性
//
// 依赖:
//   - AsioModbusTcpClient (被测对象)
//   - diagslave 或真实 Modbus TCP 服务器 (可选，用于连接成功场景)

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <future>

#include "infrastructure/plc/protocol/AsioModbusTcpClient.h"
#include "infrastructure/ISystemDriver.h"

using namespace plc::protocol;

// ============================================================================
// 1. 生命周期测试
// ============================================================================

TEST(AsioModbusClientTest, Constructor_InitialState_NotConnected) {
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.port =   502;
    config.unitId = 0x01;
    config.timeoutMs = 1000;
    config.reconnectIntervalMs = 2000;

    AsioModbusTcpClient client(config);

    // 构造后未调用 start()，应处于未连接状态
    EXPECT_FALSE(client.isConnected());
}

TEST(AsioModbusClientTest, Destructor_StopsCleanly_WithoutStart) {
    // 析构时即使未 start() 也应安全
    {
        AsioModbusTcpClient::Config config;
        config.host = "127.0.0.1";
        AsioModbusTcpClient client(config);
        // 析构发生在这里 —— 不应崩溃或挂起
    }
    SUCCEED(); // 到达这里即通过
}

TEST(AsioModbusClientTest, StartStop_Lifecycle_NoServerRequired) {
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.port = 502;
    config.reconnectIntervalMs = 500; // 短重连间隔加速测试

    AsioModbusTcpClient client(config);

    EXPECT_FALSE(client.isConnected());

    // start() 启动 io_context 并尝试连接
    client.start();

    // 给一点时间让连接尝试发生
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 没有服务器时，连接可能成功也可能失败，但不应崩溃
    // 重点验证 start/stop 不会死锁或崩溃
    client.stop();

    // stop() 后应断开连接
    EXPECT_FALSE(client.isConnected());
}

TEST(AsioModbusClientTest, DoubleStop_IsSafe) {
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.reconnectIntervalMs = 100;

    AsioModbusTcpClient client(config);
    client.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client.stop();
    client.stop(); // 双重 stop 不应崩溃
    SUCCEED();
}

TEST(AsioModbusClientTest, DoubleStart_IsSafe) {
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.reconnectIntervalMs = 100;

    AsioModbusTcpClient client(config);
    client.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client.start(); // 双重 start 不应崩溃
    client.stop();
    SUCCEED();
}

// ============================================================================
// 2. 连接状态测试
// ============================================================================

TEST(AsioModbusClientTest, IsConnected_ReflectsConnectionState) {
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.port = 502;
    config.reconnectIntervalMs = 200;

    AsioModbusTcpClient client(config);

    EXPECT_FALSE(client.isConnected());
    client.start();

    // 给连接尝试时间
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // 如果有服务器在监听 502，可能已连接；否则未连接
    // 至少验证 isConnected() 可以被调用且不崩溃
    bool state = client.isConnected();
    (void)state; // 仅验证调用安全

    client.stop();
    EXPECT_FALSE(client.isConnected());
}

// ============================================================================
// 3. 无效连接测试（错误场景）
// ============================================================================

TEST(AsioModbusClientTest, ConnectToInvalidHost_ReturnsNetworkError) {
    // 使用 TEST-NET-1 地址 (RFC 5737)，保证不可达
    AsioModbusTcpClient::Config config;
    config.host = "192.0.2.1"; // 不可路由的测试地址
    config.port = 502;
    config.timeoutMs = 500;
    config.reconnectIntervalMs = 2000;

    AsioModbusTcpClient client(config);
    client.start();

    // 给足够时间尝试连接
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    // 对无效主机调用读操作应返回网络错误
    std::vector<uint16_t> payload;
    auto result = client.readHoldingRegisters(0x0000, 1, payload);

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.isNetworkIssue());

    client.stop();
}

TEST(AsioModbusClientTest, OperationWithoutStart_ReturnsDisconnected) {
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.port = 502;

    AsioModbusTcpClient client(config);

    // 未调用 start()，直接操作应返回 Disconnected
    std::vector<uint8_t> coilPayload;
    auto result = client.readCoils(0x0000, 8, coilPayload);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::Disconnected);
}

TEST(AsioModbusClientTest, OperationAfterStop_ReturnsDisconnected) {
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.port = 502;
    config.reconnectIntervalMs = 100;

    AsioModbusTcpClient client(config);
    client.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client.stop();

    // stop() 后操作应返回 Disconnected
    std::vector<uint16_t> payload;
    auto result = client.readHoldingRegisters(0x0000, 1, payload);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::Disconnected);
}

// ============================================================================
// 4. 连接成功场景（需 diagslave 运行中）
// ============================================================================

TEST(AsioModbusClientTest, ConnectToDiagslave_IsConnected) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.port =   502;
    config.unitId = 0x01;
    config.timeoutMs = 1000;
    config.reconnectIntervalMs = 500;

    AsioModbusTcpClient client(config);
    client.start();

    // 等待连接建立
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_TRUE(client.isConnected());

    client.stop();
    EXPECT_FALSE(client.isConnected());
}

TEST(AsioModbusClientTest, ConnectToDiagslave_ReadHoldingRegisters_Success) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.port =   502;
    config.unitId = 0x01;
    config.timeoutMs = 1000;
    config.reconnectIntervalMs = 500;

    AsioModbusTcpClient client(config);
    client.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    ASSERT_TRUE(client.isConnected());

    std::vector<uint16_t> payload;
    auto result = client.readHoldingRegisters(0x0000, 2, payload);

    EXPECT_TRUE(result.ok()) << "Diagnostic: " << result.diagnostic;
    EXPECT_EQ(result.status, CommunicationResult::Status::Sent);
    EXPECT_EQ(payload.size(), 2);

    client.stop();
}

// ============================================================================
// 5. 事务 ID 单调递增测试
// ============================================================================

TEST(AsioModbusClientTest, TransactionId_IncrementsMonotonically) {
    // 这个测试验证事务 ID 的内部递增逻辑
    // 因为我们无法直接访问 nextTransactionId() 私有方法，
    // 通过多次写入操作间接触发 TID 递增，验证不会崩溃
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.port = 502;
    config.timeoutMs = 200;
    config.reconnectIntervalMs = 500;

    AsioModbusTcpClient client(config);
    client.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // 执行多次操作触达 TID 递增逻辑，验证不会崩溃
    for (int i = 0; i < 5; ++i) {
        std::vector<uint16_t> payload;
        auto result = client.readHoldingRegisters(0x0000, 1, payload);
        (void)result; // 无论成功失败，重点是 TID 递增不崩溃
    }

    client.stop();
}

// ============================================================================
// 6. 写接口连接状态测试
// ============================================================================

TEST(AsioModbusClientTest, WriteOperationAfterStop_ReturnsDisconnected) {
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.port = 502;
    config.reconnectIntervalMs = 100;

    AsioModbusTcpClient client(config);
    client.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client.stop();

    auto result = client.writeSingleCoil(0x0000, true);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::Disconnected);
}
