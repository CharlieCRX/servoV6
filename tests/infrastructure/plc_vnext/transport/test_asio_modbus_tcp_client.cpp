// ============================================================================
// test_asio_modbus_tcp_client.cpp —— Step 5 transport: 真实通讯客户端 TDD 测试
// ============================================================================
// 阶段 1「真实通讯客户端」测试（红→绿）。用自包含的本地 Modbus TCP 测试服务器
// （FakeModbusTcpServer）驱动，不依赖真实 PLC / diagslave。覆盖：
//   - 生命周期：start/stop、连接状态机、requestReconnect；
//   - FC01/FC03 读、FC05/FC06/FC10 写；
//   - 0 基址地址原样透传（不加 1 / 40001）；
//   - Modbus 异常响应转换（ProtocolError / Busy）；
//   - MBAP 校验、Transaction ID 校验、长度校验、超时、断线、重连；
//   - 读写错误保留诊断信息（endpoint + 功能码 + 地址 + 错误类型）。
// ============================================================================
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/transport/AsioModbusTcpClient.h"
#include "tests/infrastructure/plc_vnext/transport/support/FakeModbusTcpServer.h"

namespace plc_vnext::transport {
namespace {

using contracts::CommunicationResult;
using test_support::FakeModbusTcpServer;

// 统一测试台：每个用例一个独立服务器 + 客户端
class AsioModbusTcpClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        (void)server_.start();  // nodiscard：端口随后经 server_.port() 读取
        config_.host = "127.0.0.1";
        config_.port = server_.port();
        config_.unitId = 0x01;
        config_.timeoutMs = 400;
        config_.reconnectIntervalMs = 150;
    }

    void TearDown() override {
        client_.reset();
        server_.stop();
    }

    // 创建并启动客户端，等待连接建立
    std::unique_ptr<AsioModbusTcpClient> createAndStart() {
        auto c = std::make_unique<AsioModbusTcpClient>(config_);
        c->start();
        return c;
    }

    static bool waitConnected(AsioModbusTcpClient& c, int maxMs = 2000) {
        auto t0 = std::chrono::steady_clock::now();
        while (!c.isConnected()) {
            if (std::chrono::steady_clock::now() - t0 >
                std::chrono::milliseconds(maxMs))
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        return true;
    }

    FakeModbusTcpServer server_;
    AsioModbusTcpClient::Config config_;
    std::unique_ptr<AsioModbusTcpClient> client_;
};

// ════════════════════════════════════════════════════════════════════════
//  生命周期
// ════════════════════════════════════════════════════════════════════════

TEST_F(AsioModbusTcpClientTest, Constructor_NotStarted_IsNotConnected) {
    AsioModbusTcpClient client(config_);
    EXPECT_FALSE(client.isConnected());
}

TEST_F(AsioModbusTcpClientTest, Start_ConnectsToLocalServer) {
    client_ = createAndStart();
    EXPECT_TRUE(waitConnected(*client_)) << "should connect to local server";
}

TEST_F(AsioModbusTcpClientTest, Stop_SetsNotConnected) {
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));
    client_->stop();
    EXPECT_FALSE(client_->isConnected());
}

TEST_F(AsioModbusTcpClientTest, DoubleStartDoubleStop_IsSafe) {
    client_ = createAndStart();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    client_->start();  // 二次 start 应无操作
    client_->stop();
    client_->stop();   // 二次 stop 应无操作
    EXPECT_FALSE(client_->isConnected());
}

TEST_F(AsioModbusTcpClientTest, StartWithoutServer_IsNotConnected_NoCrash) {
    AsioModbusTcpClient::Config cfg = config_;
    cfg.port = 49901;  // 无人监听的端口
    AsioModbusTcpClient client(cfg);
    client.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(client.isConnected());  // 连不上
    client.requestReconnect();           // 验证不崩溃/不挂起
    client.stop();
}

// ════════════════════════════════════════════════════════════════════════
//  读通道（FC01 / FC03）
// ════════════════════════════════════════════════════════════════════════

TEST_F(AsioModbusTcpClientTest, ReadHoldingRegisters_ReturnsScriptedWords) {
    server_.setRegister(100, 0xABCD);
    server_.setRegister(101, 0x1234);
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    std::vector<uint16_t> payload;
    auto res = client_->readHoldingRegisters(100, 2, payload);
    ASSERT_TRUE(res.ok()) << res.diagnostic;
    ASSERT_EQ(payload.size(), 2u);
    EXPECT_EQ(payload[0], 0xABCD);
    EXPECT_EQ(payload[1], 0x1234);
}

TEST_F(AsioModbusTcpClientTest, ReadCoils_ReturnsScriptedBits) {
    server_.setCoil(5, true);
    server_.setCoil(7, true);
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    std::vector<uint8_t> bits;
    auto res = client_->readCoils(5, 3, bits);
    ASSERT_TRUE(res.ok()) << res.diagnostic;
    ASSERT_EQ(bits.size(), 1u);
    EXPECT_TRUE((bits[0] & 0x01) != 0);  // 线圈 5
    EXPECT_TRUE((bits[0] & 0x04) != 0);  // 线圈 7
}

// ════════════════════════════════════════════════════════════════════════
//  写通道（FC05 / FC06 / FC10）+ 0 基址透传
// ════════════════════════════════════════════════════════════════════════

TEST_F(AsioModbusTcpClientTest, WriteSingleCoil_SetsCoil_ZeroBasedAddress) {
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    auto res = client_->writeSingleCoil(0, true);  // 地址 0 必须原样
    ASSERT_TRUE(res.ok()) << res.diagnostic;

    // 写生效：服务器 RAM 中线圈 0 为 ON
    auto coil = server_.coil(0);
    ASSERT_TRUE(coil.has_value());
    EXPECT_TRUE(*coil);

    // 请求记录：0 基址地址原样（绝不是 1 或 00001 显示地址）
    auto reqs = server_.receivedRequests();
    ASSERT_FALSE(reqs.empty());
    EXPECT_EQ(reqs.back().functionCode, 0x05);
    ASSERT_GE(reqs.back().data.size(), 2u);
    uint16_t addr = static_cast<uint16_t>((reqs.back().data[0] << 8) | reqs.back().data[1]);
    EXPECT_EQ(addr, 0u);
}

TEST_F(AsioModbusTcpClientTest, WriteSingleRegister_SetsRegister_ZeroBasedAddress) {
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    auto res = client_->writeSingleRegister(0, 0x1234);
    ASSERT_TRUE(res.ok()) << res.diagnostic;

    auto r = server_.reg(0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 0x1234u);

    auto reqs = server_.receivedRequests();
    ASSERT_FALSE(reqs.empty());
    EXPECT_EQ(reqs.back().functionCode, 0x06);
    ASSERT_GE(reqs.back().data.size(), 2u);
    uint16_t addr = static_cast<uint16_t>((reqs.back().data[0] << 8) | reqs.back().data[1]);
    EXPECT_EQ(addr, 0u);
}

TEST_F(AsioModbusTcpClientTest, WriteMultipleRegisters_WritesSequence_ZeroBasedAddress) {
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    std::vector<uint16_t> values{0x0001, 0x0002, 0x0003};
    auto res = client_->writeMultipleRegisters(0, values);  // 从地址 0 写
    ASSERT_TRUE(res.ok()) << res.diagnostic;

    // 写后读回验证生效
    EXPECT_EQ(*server_.reg(0), 0x0001u);
    EXPECT_EQ(*server_.reg(2), 0x0003u);

    auto reqs = server_.receivedRequests();
    ASSERT_FALSE(reqs.empty());
    EXPECT_EQ(reqs.back().functionCode, 0x10);
    ASSERT_GE(reqs.back().data.size(), 2u);
    uint16_t addr = static_cast<uint16_t>((reqs.back().data[0] << 8) | reqs.back().data[1]);
    EXPECT_EQ(addr, 0u);  // 0 基址原样
}

// ════════════════════════════════════════════════════════════════════════
//  0 基址读透传（服务器应收到地址 0，而非 +1 / 40001）
// ════════════════════════════════════════════════════════════════════════

TEST_F(AsioModbusTcpClientTest, ReadZeroBasedAddress_ServerReceivesZero) {
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    std::vector<uint16_t> payload;
    ASSERT_TRUE(client_->readHoldingRegisters(0, 1, payload).ok());

    auto reqs = server_.receivedRequests();
    ASSERT_FALSE(reqs.empty());
    EXPECT_EQ(reqs.back().functionCode, 0x03);
    ASSERT_GE(reqs.back().data.size(), 2u);
    uint16_t addr = static_cast<uint16_t>((reqs.back().data[0] << 8) | reqs.back().data[1]);
    EXPECT_EQ(addr, 0u);
}

// ════════════════════════════════════════════════════════════════════════
//  Modbus 异常响应转换
// ════════════════════════════════════════════════════════════════════════

TEST_F(AsioModbusTcpClientTest, ModbusException_ReturnsProtocolError_WithCode) {
    server_.scriptException(0x02);  // Illegal Data Address
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    std::vector<uint16_t> payload;
    auto res = client_->readHoldingRegisters(100, 1, payload);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status, CommunicationResult::Status::ProtocolError);
    EXPECT_EQ(res.exceptionCode, 0x02);
    EXPECT_TRUE(res.isProtocolIssue());
}

TEST_F(AsioModbusTcpClientTest, BusyException_ReturnsBusy_Retryable) {
    server_.scriptException(0x06);  // Server Device Busy
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    std::vector<uint16_t> payload;
    auto res = client_->readHoldingRegisters(100, 1, payload);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status, CommunicationResult::Status::Busy);
    EXPECT_EQ(res.exceptionCode, 0x06);
    EXPECT_TRUE(res.retryable());
}

// ════════════════════════════════════════════════════════════════════════
//  协议校验：坏 MBAP / 错误 TID / 长度不符
// ════════════════════════════════════════════════════════════════════════

TEST_F(AsioModbusTcpClientTest, BadMbap_ReturnsInvalidResponse) {
    server_.scriptBadMbap();
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    std::vector<uint16_t> payload;
    auto res = client_->readHoldingRegisters(100, 1, payload);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status, CommunicationResult::Status::InvalidResponse);
}

TEST_F(AsioModbusTcpClientTest, BadTransactionId_ReturnsInvalidResponse) {
    server_.scriptBadTid();
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    std::vector<uint16_t> payload;
    auto res = client_->readHoldingRegisters(100, 1, payload);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status, CommunicationResult::Status::InvalidResponse);
}

TEST_F(AsioModbusTcpClientTest, LengthMismatch_ReturnsFailure_NotForgedSuccess) {
    server_.scriptInvalidLength();
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    std::vector<uint16_t> payload;
    auto res = client_->readHoldingRegisters(100, 1, payload);
    // 长度不符不能伪装成成功
    EXPECT_FALSE(res.ok());
    EXPECT_TRUE(payload.empty());
}

// ════════════════════════════════════════════════════════════════════════
//  超时 / 断线 / 重连
// ════════════════════════════════════════════════════════════════════════

TEST_F(AsioModbusTcpClientTest, NoResponse_ReturnsTimeout) {
    server_.scriptNoResponse();
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    auto t0 = std::chrono::steady_clock::now();
    std::vector<uint16_t> payload;
    auto res = client_->readHoldingRegisters(100, 1, payload);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0)
                       .count();
    EXPECT_EQ(res.status, CommunicationResult::Status::Timeout);
    EXPECT_TRUE(res.retryable());
    EXPECT_TRUE(res.isNetworkIssue());
    EXPECT_LT(elapsed, 3000) << "timeout must not hang the caller";
}

TEST_F(AsioModbusTcpClientTest, DelayedResponse_ExceedingTimeout_ReturnsTimeout) {
    server_.scriptDelayMs(1500);  // 超过 timeoutMs=400
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    std::vector<uint16_t> payload;
    auto res = client_->readHoldingRegisters(100, 1, payload);
    EXPECT_EQ(res.status, CommunicationResult::Status::Timeout);
}

// 超时后后台事务可能仍在 io 线程收尾（其响应通过 TransactionOutcome 经 promise
// 传回、被已放弃的 future 自然丢弃，不访问任何失效引用）。本用例验证超时后客户端
// 仍能重连并正常通讯，证明晚到结果被安全处理、未破坏状态（行为上排除悬空引用 UB）。
TEST_F(AsioModbusTcpClientTest, Timeout_ThenSubsequentTransaction_Succeeds) {
    server_.scriptDelayMs(1500);
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    std::vector<uint16_t> payload;
    auto res = client_->readHoldingRegisters(100, 1, payload);
    EXPECT_EQ(res.status, CommunicationResult::Status::Timeout);

    // 等待重连后，后续事务应正常成功
    bool recovered = false;
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(3000)) {
        auto r = client_->readHoldingRegisters(100, 1, payload);
        if (r.ok()) { recovered = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_TRUE(recovered) << "client should recover and transact after a timeout";
    ASSERT_FALSE(payload.empty());
    EXPECT_EQ(payload[0], 0u);  // 寄存器 100 未预置 -> 0
}

TEST_F(AsioModbusTcpClientTest, ServerDisconnect_AfterResponse_ThenReconnect) {
    server_.scriptDisconnectAfterResponse();
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    // 第一次读成功
    std::vector<uint16_t> payload;
    ASSERT_TRUE(client_->readHoldingRegisters(100, 1, payload).ok());

    // 服务器断线后，客户端应通过自动重连恢复（重连间隔 150ms）
    bool recovered = false;
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(3000)) {
        auto res = client_->readHoldingRegisters(100, 1, payload);
        if (res.ok()) { recovered = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_TRUE(recovered) << "client should auto-reconnect after server disconnect";
}

// ════════════════════════════════════════════════════════════════════════
//  诊断信息：读写错误必须保留 endpoint + 功能码 + 地址 + 错误类型
// ════════════════════════════════════════════════════════════════════════

TEST_F(AsioModbusTcpClientTest, ErrorDiagnostic_ContainsEndpointFcAddressType) {
    server_.scriptException(0x03);
    client_ = createAndStart();
    ASSERT_TRUE(waitConnected(*client_));

    std::vector<uint16_t> payload;
    auto res = client_->readHoldingRegisters(100, 1, payload);
    ASSERT_FALSE(res.ok());

    // diagnostic 必须定位到 endpoint、功能码、地址与错误类型
    EXPECT_NE(res.diagnostic.find("127.0.0.1"), std::string::npos)
        << "diagnostic must contain endpoint";
    EXPECT_NE(res.diagnostic.find("0x03"), std::string::npos)
        << "diagnostic must contain function code";
    EXPECT_NE(res.diagnostic.find("0x0064"), std::string::npos)  // addr=100
        << "diagnostic must contain address";
    EXPECT_NE(res.diagnostic.find("exception"), std::string::npos)
        << "diagnostic must contain error type";
}

TEST_F(AsioModbusTcpClientTest, NotConnected_ReturnsDisconnected_WithEndpoint) {
    AsioModbusTcpClient client(config_);  // 未 start
    std::vector<uint16_t> payload;
    auto res = client.readHoldingRegisters(100, 1, payload);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status, CommunicationResult::Status::Disconnected);
    EXPECT_TRUE(res.isDisconnected());
    EXPECT_NE(res.diagnostic.find("127.0.0.1"), std::string::npos);
}

}  // namespace
}  // namespace plc_vnext::transport
