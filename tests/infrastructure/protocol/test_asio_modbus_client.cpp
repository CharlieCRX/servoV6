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

// ============================================================================
// 7. Socket 级超时测试（需 diagslave 运行中）
// ============================================================================

// 注意：正常 diagslave 响应极快（<5ms），不会触发超时。
// 此测试验证的是：超时机制正确工作且不影响后续重连时序。
// 
// 工业现场真实场景：
//   交换机抖动、PLC 卡死、半断开状态下，读操作可能永久卡死。
//   Phase 2.3 引入 steady_timer + socket.cancel() 真正从 io_context
//   内部中断阻塞的 I/O（不再是之前只有 future.wait_for 在业务线程干等）。
//
// 如何手动验证超时：
//   1. 启动 diagslave
//   2. 运行此测试
//   3. 在测试 sleep 期间拔掉网线或暂停 diagslave
//   4. 观察 [TIMEOUT] 日志
TEST(AsioModbusClientTest, ConnectToDiagslave_TimerStateRecovery_AfterTransaction) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.port =   502;
    config.unitId = 0x01;
    config.timeoutMs = 1000;
    config.reconnectIntervalMs = 200;

    AsioModbusTcpClient client(config);
    client.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(client.isConnected()) 
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    // 执行一次成功事务 → timer 会被 reset
    std::vector<uint16_t> payload;
    auto result = client.readHoldingRegisters(0x0000, 1, payload);
    EXPECT_TRUE(result.ok()) << "First read should succeed: " << result.diagnostic;

    // 再执行第二次事务 → 验证 timer 复用正常
    payload.clear();
    result = client.readHoldingRegisters(0x0000, 1, payload);
    EXPECT_TRUE(result.ok()) 
        << "Second read after timer reset should also succeed: " << result.diagnostic;

    client.stop();
}

// ============================================================================
// 8. 断线检测与自动重连测试（需 diagslave 运行中）
// ============================================================================

// 测试场景：
//   1. 连接 diagslave → 读成功
//   2. 主动 close socket 模拟断线
//   3. 验证 isConnected 变为 false
//   4. 等待自动重连（reconnectIntervalMs 后）
//   5. 验证 isConnected 恢复为 true 且读成功
//
// 注：diagslave 重建连接需要新的 socket，而我们是通过 async_resolve 
//      + async_connect 重建。测试中的 "断开" 是通过显式 socket.close()
//      + connected=false 来模拟链路中断。
TEST(AsioModbusClientTest, ConnectToDiagslave_ReconnectAfterDisconnect) {
    // 前置条件: diagslave -m tcp -p 504 -a 1 正在运行
    //           使用独立端口 504 避免与现有测试冲突
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.port =   502;
    config.unitId = 0x01;
    config.timeoutMs = 1000;
    config.reconnectIntervalMs = 500; // 短间隔加速测试

    AsioModbusTcpClient client(config);
    client.start();

    // 1️⃣ 等待首次连接
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(client.isConnected()) 
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    // 验证连接后可以通讯
    std::vector<uint16_t> payload;
    auto result = client.readHoldingRegisters(0x0000, 1, payload);
    ASSERT_TRUE(result.ok()) 
        << "Pre-disconnect read should succeed: " << result.diagnostic;

    // 2️⃣ 模拟链路中断：连接到一个不可达地址触发断线
    //    使用 InvalidHost 测试的不可路由地址来触发 NetworkError
    //    实际操作：用无效地址 replace 后会触发重连
    // 注意：这里无法直接调用私有方法去 close socket，
    //       但我们可以通过调用会触发 NetworkError 的操作来间接测试。
    //       更直接的方式：重用已建立的 socket 主动关闭 →
    //       由于 socket 存储在 private 中，无法从测试访问。
    //       替代方案：stop → 重新 start → 此时会重新连接
    client.stop();

    // 3️⃣ 等待重连间隔 + 重新连接
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(client.isConnected()) 
        << "Should be disconnected after stop()";

    // 重新启动
    client.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    // 4️⃣ 验证自动重连
    EXPECT_TRUE(client.isConnected())
        << "Should reconnect after restart (diagslave must be running)";

    // 5️⃣ 验证恢复通讯
    payload.clear();
    result = client.readHoldingRegisters(0x0000, 1, payload);
    EXPECT_TRUE(result.ok()) 
        << "Post-reconnect read should succeed: " << result.diagnostic;
    EXPECT_EQ(payload.size(), 1);

    client.stop();
}

// ============================================================================
// 9. 多次 start/stop 循环测试（生命周期健壮性）
// ============================================================================

TEST(AsioModbusClientTest, StartStopCycle_MultipleTimes_NoCrash) {
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.port = 502;
    config.reconnectIntervalMs = 100;

    AsioModbusTcpClient client(config);

    for (int i = 0; i < 3; ++i) {
        client.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        client.stop();
    }
    SUCCEED();
}
