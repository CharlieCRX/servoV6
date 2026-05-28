// tests/infrastructure/protocol/test_modbus_tcp_integration.cpp
// P4 Phase 4 — 端到端集成验证 (TDD)
//
// 测试范围:
//   - PlcPoller + AsioModbusTcpClient 联合轮询管线
//   - PlcDevice + AsioModbusTcpClient 完整写通道
//   - 混合读写场景（模拟真实主循环）
//   - 连续压力测试（100+ 请求稳定性）
//   - 断线→重连→恢复通讯全链路
//   - CommunicationResult 状态映射完整性
//
// 前置条件:
//   diagslave -m tcp -p 502 -a 1 必须在运行中
//
// 依赖:
//   - AsioModbusTcpClient (L3 传输层)
//   - PlcPoller             (L2 协议运行时 — 轮询)
//   - PlcDevice             (L1 驱动集成层 — 读写门面)
//   - RegisterRegistry      (寄存器声明)
//   - ProtocolProfile       (端序策略)

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdint>
#include <memory>

#include "infrastructure/plc/protocol/AsioModbusTcpClient.h"
#include "infrastructure/plc/protocol/PlcPoller.h"
#include "infrastructure/plc/protocol/PlcDevice.h"
#include "infrastructure/plc/protocol/RegisterRegistry.h"
#include "infrastructure/plc/protocol/ProtocolProfile.h"
#include "infrastructure/plc/protocol/RegisterMetadata.h"
#include "infrastructure/ISystemDriver.h"

using namespace plc::protocol;

// ============================================================================
// 测试 Fixture：提供统一的 diagslave 连接配置与客户端生命周期管理
// ============================================================================

class ModbusTcpIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.host = "127.0.0.1";
        config_.port = 502;
        config_.unitId = 0x01;
        config_.timeoutMs = 1000;
        config_.reconnectIntervalMs = 500;
    }

    void TearDown() override {
        // 各测试自行管理 client 生命周期
    }

    /// @brief 创建并启动客户端，等待连接建立
    /// @return 已连接的客户端（调用方负责 stop）
    std::unique_ptr<AsioModbusTcpClient> createAndStart() {
        auto client = std::make_unique<AsioModbusTcpClient>(config_);
        client->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        return client;
    }

    /// @brief 等待连接建立（带超时断言）
    void waitForConnection(AsioModbusTcpClient& client, int maxWaitMs = 1000) {
        auto start = std::chrono::steady_clock::now();
        while (!client.isConnected()) {
            if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(maxWaitMs)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    /// @brief 创建 RegisterInfo 辅助工厂
    static RegisterInfo makeCoil(uint16_t addr, RegisterAccess acc,
                                  RegisterBehavior beh, RegisterGroup grp,
                                  const char* desc) {
        return RegisterInfo{
            RegisterArea::Coil, addr, RegisterType::Bool, acc,
            beh, grp, "", desc, 0, std::nullopt
        };
    }

    static RegisterInfo makeHoldingReg(uint16_t addr, RegisterType type,
                                        RegisterAccess acc, RegisterBehavior beh,
                                        RegisterGroup grp, const char* desc) {
        return RegisterInfo{
            RegisterArea::HoldingReg, addr, type, acc,
            beh, grp, "", desc, 0, std::nullopt
        };
    }

    AsioModbusTcpClient::Config config_;
};

// ============================================================================
// 1. 完整读管线：PlcPoller + AsioModbusTcpClient 联合轮询
// ============================================================================

TEST_F(ModbusTcpIntegrationTest, PlcPoller_ExecuteAndAssemble_Success) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行

    // 1️⃣ 构建 RegisterRegistry（模拟真实寄存器表）
    RegisterRegistry registry;
    registry.addAll({
        makeCoil(0x0000, RegisterAccess::ReadOnly,
                 RegisterBehavior::Continuous, RegisterGroup::Feedback, "coil_0"),
        makeCoil(0x0001, RegisterAccess::ReadOnly,
                 RegisterBehavior::Continuous, RegisterGroup::Feedback, "coil_1"),
        makeHoldingReg(0x0064, RegisterType::Int16, RegisterAccess::ReadOnly,
                       RegisterBehavior::Continuous, RegisterGroup::Feedback, "reg_100"),
        makeHoldingReg(0x0065, RegisterType::Int16, RegisterAccess::ReadOnly,
                       RegisterBehavior::Continuous, RegisterGroup::Feedback, "reg_101"),
    });

    PlcPoller poller(registry);
    ProtocolProfile profile; // 默认 Big-Endian

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected()) 
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    // 2️⃣ prepare() 生成请求列表
    auto req = poller.prepare();
    EXPECT_FALSE(req.coilRequests.empty()) << "Should have coil requests for registered coils";
    EXPECT_FALSE(req.wordRequests.empty()) << "Should have word requests for registered holding regs";

    // 3️⃣ 通过 AsioModbusTcpClient 执行每个请求
    std::vector<std::vector<uint8_t>>  coilResponses;
    std::vector<std::vector<uint16_t>> wordResponses;

    for (const auto& coilReq : req.coilRequests) {
        std::vector<uint8_t> payload;
        auto result = client->readCoils(coilReq.range.startAddress, coilReq.range.count, payload);
        ASSERT_TRUE(result.ok()) 
            << "readCoils failed: " << result.diagnostic
            << " addr=" << coilReq.range.startAddress 
            << " count=" << coilReq.range.count;
        coilResponses.push_back(std::move(payload));
    }

    for (const auto& wordReq : req.wordRequests) {
        std::vector<uint16_t> payload;
        auto result = client->readHoldingRegisters(
            wordReq.range.startAddress, wordReq.range.count, payload);
        ASSERT_TRUE(result.ok()) 
            << "readHoldingRegisters failed: " << result.diagnostic
            << " addr=" << wordReq.range.startAddress 
            << " count=" << wordReq.range.count;
        wordResponses.push_back(std::move(payload));
    }

    // 4️⃣ assemble() 汇编为 PlcSnapshot
    auto snapshot = poller.assemble(coilResponses, wordResponses, 12345);
    EXPECT_TRUE(snapshot.isTrusted()) << "Snapshot should be trusted when all reads succeed";
    EXPECT_EQ(snapshot.timestamp, 12345);

    // 5️⃣ 用 PlcDevice 从 snapshot 读取值
    PlcDevice device(profile);
    device.updateSnapshot(std::move(snapshot));

    // 从 snapshot 解码，验证不抛异常（diagslave 返回真实值，我们不验证具体值）
    const RegisterInfo* pCoil = registry.findByAddress(RegisterArea::Coil, 0x0000);
    ASSERT_NE(pCoil, nullptr) << "Coil 0x0000 must exist in registry";
    PlcValue coilVal = device.readValue(*pCoil);
    EXPECT_NO_THROW({
        auto b = getValue<bool>(coilVal);
        (void)b;
    }) << "Should decode coil 0 as Bool";

    const RegisterInfo* pReg = registry.findByAddress(RegisterArea::HoldingReg, 0x0064);
    ASSERT_NE(pReg, nullptr) << "Register 0x0064 must exist in registry";
    PlcValue regVal = device.readValue(*pReg);
    EXPECT_NO_THROW({
        auto v = getValue<int16_t>(regVal);
        (void)v;
    }) << "Should decode register 100 as Int16";

    client->stop();
}

// ============================================================================
// 2. 完整写管线：PlcDevice + AsioModbusTcpClient
// ============================================================================

TEST_F(ModbusTcpIntegrationTest, PlcDevice_WriteSingleCoil_Success) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行

    ProtocolProfile profile;
    PlcDevice device(profile);

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected()) 
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    device.bindTransport(client.get());

    // 向可写线圈地址 10 写入 true
    RegisterInfo writeReg = makeCoil(0x000A, RegisterAccess::ReadWrite,
                                      RegisterBehavior::AutoResetEdgeTrigger,
                                      RegisterGroup::Command, "write_coil_10");

    auto result = device.writeBool(writeReg, true);
    EXPECT_TRUE(result.ok()) << "writeBool should succeed: " << result.diagnostic;
    EXPECT_EQ(result.status, CommunicationResult::Status::Sent);

    // 回读验证（diagslave 会回显写入值）
    std::vector<uint8_t> payload;
    auto readResult = client->readCoils(0x000A, 1, payload);
    EXPECT_TRUE(readResult.ok()) << "Readback should succeed: " << readResult.diagnostic;

    client->stop();
}

TEST_F(ModbusTcpIntegrationTest, PlcDevice_WriteSingleRegister_Success) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行

    ProtocolProfile profile;
    PlcDevice device(profile);

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected()) 
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    device.bindTransport(client.get());

    RegisterInfo writeReg = makeHoldingReg(0x00C8, RegisterType::Int16,
                                            RegisterAccess::ReadWrite,
                                            RegisterBehavior::AutoResetEdgeTrigger,
                                            RegisterGroup::Command, "write_reg_200");

    auto result = device.writeInt16(writeReg, 42);
    EXPECT_TRUE(result.ok()) << "writeInt16 should succeed: " << result.diagnostic;
    EXPECT_EQ(result.status, CommunicationResult::Status::Sent);

    // 回读验证
    std::vector<uint16_t> payload;
    auto readResult = client->readHoldingRegisters(0x00C8, 1, payload);
    EXPECT_TRUE(readResult.ok()) << "Readback should succeed: " << readResult.diagnostic;
    if (readResult.ok() && !payload.empty()) {
        EXPECT_EQ(payload[0], 42) << "Diagslave should echo written register value";
    }

    client->stop();
}

TEST_F(ModbusTcpIntegrationTest, PlcDevice_WriteMultipleRegisters_Success) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行

    ProtocolProfile profile;
    PlcDevice device(profile);

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected()) 
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    device.bindTransport(client.get());

    // 写入 Float32 到连续 2 个寄存器 (FC10)
    RegisterInfo writeReg = makeHoldingReg(0x012C, RegisterType::Float32,
                                            RegisterAccess::ReadWrite,
                                            RegisterBehavior::AutoResetEdgeTrigger,
                                            RegisterGroup::Command, "write_float_300");

    auto result = device.writeFloat(writeReg, 3.14f);
    EXPECT_TRUE(result.ok()) << "writeFloat (FC10) should succeed: " << result.diagnostic;
    EXPECT_EQ(result.status, CommunicationResult::Status::Sent);

    // 回读 2 个寄存器验证写入成功（非零即可，具体值取决于 endian）
    std::vector<uint16_t> payload;
    auto readResult = client->readHoldingRegisters(0x012C, 2, payload);
    EXPECT_TRUE(readResult.ok()) << "FC10 readback should succeed: " << readResult.diagnostic;
    if (readResult.ok()) {
        EXPECT_EQ(payload.size(), 2) << "Should read back 2 registers for Float32";
    }

    client->stop();
}

// ============================================================================
// 3. 写通道错误场景
// ============================================================================

TEST_F(ModbusTcpIntegrationTest, WriteWithoutTransport_ThrowsRuntimeError) {
    ProtocolProfile profile;
    PlcDevice device(profile);
    // 故意不 bindTransport

    RegisterInfo reg = makeCoil(0x0000, RegisterAccess::WriteOnly,
                                 RegisterBehavior::AutoResetEdgeTrigger,
                                 RegisterGroup::Command, "test_coil");

    EXPECT_THROW({
        device.writeBool(reg, true);
    }, std::runtime_error) << "Should throw when no transport bound";
}

TEST_F(ModbusTcpIntegrationTest, WriteToReadOnly_ThrowsInvalidArgument) {
    ProtocolProfile profile;
    PlcDevice device(profile);

    RegisterInfo readOnlyReg = makeCoil(0x0000, RegisterAccess::ReadOnly,
                                         RegisterBehavior::Continuous,
                                         RegisterGroup::Feedback, "readonly_coil");

    EXPECT_THROW({
        device.writeBool(readOnlyReg, true);
    }, std::invalid_argument) << "Should throw for ReadOnly register";
}

// ============================================================================
// 4. 连续压力测试：验证通讯稳定性
// ============================================================================

TEST_F(ModbusTcpIntegrationTest, StressTest_100Reads_NoFailures) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected()) 
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    int successCount = 0;
    int failureCount = 0;

    for (int i = 0; i < 100; ++i) {
        std::vector<uint16_t> payload;
        auto result = client->readHoldingRegisters(0x0000, 2, payload);
        if (result.ok()) {
            ++successCount;
        } else {
            ++failureCount;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    EXPECT_EQ(failureCount, 0) << "All 100 reads should succeed against local diagslave";
    EXPECT_EQ(successCount, 100);

    client->stop();
}

TEST_F(ModbusTcpIntegrationTest, StressTest_MixedReadWrite_100Cycles) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行
    // 模拟真实主循环：每个 cycle 先读寄存器，再写命令

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected()) 
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    ProtocolProfile profile;
    PlcDevice device(profile);
    device.bindTransport(client.get());

    int cyclesOk = 0;
    int cyclesFailed = 0;

    RegisterInfo writeReg = makeHoldingReg(0x00C8, RegisterType::Int16,
                                            RegisterAccess::ReadWrite,
                                            RegisterBehavior::AutoResetEdgeTrigger,
                                            RegisterGroup::Command, "cmd_reg_200");

    for (int i = 0; i < 100; ++i) {
        bool cycleOk = true;

        // 模拟读操作（如 PlcPoller 轮询）
        {
            std::vector<uint16_t> payload;
            auto result = client->readHoldingRegisters(0x0000, 2, payload);
            if (!result.ok()) {
                cycleOk = false;
            }
        }

        // 模拟写操作（通过 PlcDevice）
        {
            auto result = device.writeInt16(writeReg, static_cast<int16_t>(i));
            if (!result.ok()) {
                cycleOk = false;
            }
        }

        if (cycleOk) {
            ++cyclesOk;
        } else {
            ++cyclesFailed;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    EXPECT_EQ(cyclesFailed, 0) << "All mixed read-write cycles should succeed";
    EXPECT_EQ(cyclesOk, 100);

    client->stop();
}

// ============================================================================
// 5. 断线重连全链路验证
// ============================================================================

TEST_F(ModbusTcpIntegrationTest, DisconnectReconnect_FullCycle) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行
    //
    // 场景：
    //   1. 连接 diagslave → 读写成功
    //   2. stop() 模拟断线
    //   3. start() 触发重连
    //   4. 验证恢复通讯

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected()) 
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    // ═══ 阶段 1: 首次连接，读写验证 ═══
    // 验证读
    {
        std::vector<uint16_t> payload;
        auto result = client->readHoldingRegisters(0x0000, 1, payload);
        ASSERT_TRUE(result.ok()) << "Pre-disconnect read: " << result.diagnostic;
    }

    // 验证写
    {
        auto result = client->writeSingleRegister(0x00C8, 0x1234);
        ASSERT_TRUE(result.ok()) << "Pre-disconnect write: " << result.diagnostic;
    }

    // ═══ 阶段 2: 断线 ═══
    client->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(client->isConnected()) << "Should be disconnected after stop()";

    // stop 后操作应返回 Disconnected
    {
        std::vector<uint16_t> payload;
        auto result = client->readHoldingRegisters(0x0000, 1, payload);
        EXPECT_FALSE(result.ok());
        EXPECT_EQ(result.status, CommunicationResult::Status::Disconnected);
    }

    {
        auto result = client->writeSingleRegister(0x00C8, 0x5678);
        EXPECT_FALSE(result.ok());
        EXPECT_EQ(result.status, CommunicationResult::Status::Disconnected);
    }

    // ═══ 阶段 3: 重连 ═══
    client->start();
    waitForConnection(*client, 2000);
    ASSERT_TRUE(client->isConnected()) 
        << "Should reconnect to diagslave within 2s after restart";

    // ═══ 阶段 4: 恢复通讯验证 ═══
    // 读恢复
    {
        std::vector<uint16_t> payload;
        auto result = client->readHoldingRegisters(0x0000, 1, payload);
        EXPECT_TRUE(result.ok()) << "Post-reconnect read should succeed: " << result.diagnostic;
    }

    // 写恢复
    {
        auto result = client->writeSingleRegister(0x00C8, 0x9ABC);
        EXPECT_TRUE(result.ok()) << "Post-reconnect write should succeed: " << result.diagnostic;
    }

    // 回读验证 write 确实生效
    {
        std::vector<uint16_t> payload;
        auto result = client->readHoldingRegisters(0x00C8, 1, payload);
        ASSERT_TRUE(result.ok());
        if (!payload.empty()) {
            EXPECT_EQ(payload[0], 0x9ABC) 
                << "Written value should persist in diagslave after reconnect";
        }
    }

    client->stop();
}

TEST_F(ModbusTcpIntegrationTest, MultipleReconnectCycles_Stable) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行
    // 验证 5 次 stop/start 循环均能恢复正常通讯

    std::unique_ptr<AsioModbusTcpClient> client = std::make_unique<AsioModbusTcpClient>(config_);

    for (int cycle = 0; cycle < 5; ++cycle) {
        client->start();
        waitForConnection(*client, 2000);
        ASSERT_TRUE(client->isConnected()) 
            << "Cycle " << cycle << ": should connect to diagslave";

        // 读操作验证
        {
            std::vector<uint16_t> payload;
            auto result = client->readHoldingRegisters(0x0000, 1, payload);
            EXPECT_TRUE(result.ok()) 
                << "Cycle " << cycle << " read: " << result.diagnostic;
        }

        // 写操作验证
        {
            auto result = client->writeSingleRegister(0x00C8, static_cast<uint16_t>(cycle));
            EXPECT_TRUE(result.ok()) 
                << "Cycle " << cycle << " write: " << result.diagnostic;
        }

        client->stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        EXPECT_FALSE(client->isConnected()) << "Cycle " << cycle << ": should disconnect after stop";
    }
}

// ============================================================================
// 6. CommunicationResult 状态映射完整性
// ============================================================================

TEST_F(ModbusTcpIntegrationTest, CommunicationResult_Sent_HasCorrectProperties) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected()) 
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    std::vector<uint16_t> payload;
    auto result = client->readHoldingRegisters(0x0000, 1, payload);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::Sent);
    EXPECT_FALSE(result.isNetworkIssue());
    EXPECT_FALSE(result.isProtocolIssue());
    EXPECT_FALSE(result.retryable());
    EXPECT_EQ(result.exceptionCode, 0);

    client->stop();
}

TEST_F(ModbusTcpIntegrationTest, CommunicationResult_Disconnected_HasCorrectProperties) {
    AsioModbusTcpClient client(config_);
    // 不调用 start() — 处于未连接状态

    std::vector<uint8_t> payload;
    auto result = client.readCoils(0x0000, 8, payload);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::Disconnected);
    EXPECT_TRUE(result.isNetworkIssue());
    EXPECT_FALSE(result.isProtocolIssue());
    EXPECT_FALSE(result.retryable());

    client.stop();
}

// ============================================================================
// 7. 事务 ID 连续性验证（跨混合操作）
// ============================================================================

TEST_F(ModbusTcpIntegrationTest, TransactionId_Continuous_AcrossMixedOperations) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行
    //
    // 验证读写混合操作不会导致事务 ID 混乱
    // （通过连续操作无崩溃、结果正确来间接触达 TID 递增逻辑）

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected()) 
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    // 执行混合操作序列：读→写→读→写，共 50 轮
    for (int i = 0; i < 50; ++i) {
        {
            std::vector<uint16_t> payload;
            auto result = client->readHoldingRegisters(0x0000, 1, payload);
            ASSERT_TRUE(result.ok()) << "Read at iteration " << i << ": " << result.diagnostic;
        }
        {
            auto result = client->writeSingleRegister(0x00C8, static_cast<uint16_t>(i));
            ASSERT_TRUE(result.ok()) << "Write at iteration " << i << ": " << result.diagnostic;
        }
    }

    client->stop();
}

// ============================================================================
// 8. 多地址范围读取（验证 AddressPacker 集成）
// ============================================================================

TEST_F(ModbusTcpIntegrationTest, MultiRangeRead_AggregatedByAddressPacker) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行
    //
    // 验证不连续地址区间的线圈/寄存器能被正确打包和读取
    // Coil: 0~1 (连续) + 0xA0~0xA1 (远处)，maxGap=200 合并为 1 个 range

    RegisterRegistry registry;
    registry.addAll({
        makeCoil(0x0000, RegisterAccess::ReadOnly,
                 RegisterBehavior::Continuous, RegisterGroup::Feedback, "c0"),
        makeCoil(0x0001, RegisterAccess::ReadOnly,
                 RegisterBehavior::Continuous, RegisterGroup::Feedback, "c1"),
        makeCoil(0x00A0, RegisterAccess::ReadOnly,
                 RegisterBehavior::Continuous, RegisterGroup::Feedback, "c_a0"),
        makeCoil(0x00A1, RegisterAccess::ReadOnly,
                 RegisterBehavior::Continuous, RegisterGroup::Feedback, "c_a1"),
    });

    // Word: 0x64 + 0xC8 (不连续，maxGap=50 通常不合并，产生 2 个 range)
    registry.addAll({
        makeHoldingReg(0x0064, RegisterType::Int16, RegisterAccess::ReadOnly,
                       RegisterBehavior::Continuous, RegisterGroup::Feedback, "r_100"),
        makeHoldingReg(0x00C8, RegisterType::Int16, RegisterAccess::ReadOnly,
                       RegisterBehavior::Continuous, RegisterGroup::Feedback, "r_200"),
    });

    PlcPoller poller(registry);

    auto req = poller.prepare();
    // Coil maxGap=200 → 4 个 Coil 均在一个 range
    // Word maxGap=50 → gap(0x64→0xC8)=99 > 50 → 2 个 range

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected()) 
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    std::vector<std::vector<uint8_t>>  coilResponses;
    std::vector<std::vector<uint16_t>> wordResponses;

    for (const auto& cr : req.coilRequests) {
        std::vector<uint8_t> payload;
        auto result = client->readCoils(cr.range.startAddress, cr.range.count, payload);
        ASSERT_TRUE(result.ok()) 
            << "Coil read range [" << cr.range.startAddress 
            << ", " << cr.range.count << "]: " << result.diagnostic;
        coilResponses.push_back(std::move(payload));
    }

    for (const auto& wr : req.wordRequests) {
        std::vector<uint16_t> payload;
        auto result = client->readHoldingRegisters(
            wr.range.startAddress, wr.range.count, payload);
        ASSERT_TRUE(result.ok()) 
            << "Word read range [" << wr.range.startAddress 
            << ", " << wr.range.count << "]: " << result.diagnostic;
        wordResponses.push_back(std::move(payload));
    }

    auto snapshot = poller.assemble(coilResponses, wordResponses, 0);
    EXPECT_TRUE(snapshot.isTrusted());

    client->stop();
}

// ============================================================================
// 9. 模拟真实主循环轮询（统一快照管线）
// ============================================================================

TEST_F(ModbusTcpIntegrationTest, RealisticPollingCycle_MultipleIterations) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行
    //
    // 模拟真实上位机主循环：每周期执行以下步骤
    //   1. PlcPoller::prepare() 生成请求
    //   2. AsioModbusTcpClient 执行所有读请求
    //   3. PlcPoller::assemble() 组装快照
    //   4. PlcDevice::updateSnapshot() + readValue() 读取反馈
    //   5. PlcDevice::writeValue() 写入命令（条件触发）

    // 构建模拟寄存器表（与汇川 PLC 典型映射类似）
    RegisterRegistry registry;
    registry.addAll({
        // 反馈寄存器（只读）
        makeCoil(0x0000, RegisterAccess::ReadOnly,
                 RegisterBehavior::Continuous, RegisterGroup::Feedback, "fb_coil_ready"),
        makeCoil(0x0001, RegisterAccess::ReadOnly,
                 RegisterBehavior::Continuous, RegisterGroup::Feedback, "fb_coil_alarm"),
        makeHoldingReg(0x0100, RegisterType::Int16, RegisterAccess::ReadOnly,
                       RegisterBehavior::Continuous, RegisterGroup::Feedback, "fb_pos_actual"),
        makeHoldingReg(0x0101, RegisterType::Int16, RegisterAccess::ReadOnly,
                       RegisterBehavior::Continuous, RegisterGroup::Feedback, "fb_vel_actual"),

        // 命令寄存器（读写）
        makeCoil(0x0010, RegisterAccess::ReadWrite,
                 RegisterBehavior::AutoResetEdgeTrigger, RegisterGroup::Command, "cmd_enable"),
        makeHoldingReg(0x0200, RegisterType::Int16, RegisterAccess::ReadWrite,
                       RegisterBehavior::AutoResetEdgeTrigger, RegisterGroup::Command, "cmd_target_pos"),
    });

    ProtocolProfile profile;
    PlcPoller poller(registry);
    PlcDevice device(profile);

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected()) 
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    device.bindTransport(client.get());

    int trustedCycles = 0;
    int writeSuccess = 0;

    for (int cycle = 0; cycle < 20; ++cycle) {
        // ═══ Step 1: 准备请求 ═══
        auto req = poller.prepare();

        // ═══ Step 2: 执行读请求 ═══
        std::vector<std::vector<uint8_t>>  coilResponses;
        std::vector<std::vector<uint16_t>> wordResponses;

        bool allReadsOk = true;
        for (const auto& cr : req.coilRequests) {
            std::vector<uint8_t> payload;
            auto result = client->readCoils(cr.range.startAddress, cr.range.count, payload);
            if (result.ok()) {
                coilResponses.push_back(std::move(payload));
            } else {
                allReadsOk = false;
                break;
            }
        }

        if (allReadsOk) {
            for (const auto& wr : req.wordRequests) {
                std::vector<uint16_t> payload;
                auto result = client->readHoldingRegisters(
                    wr.range.startAddress, wr.range.count, payload);
                if (result.ok()) {
                    wordResponses.push_back(std::move(payload));
                } else {
                    allReadsOk = false;
                    break;
                }
            }
        }

        // ═══ Step 3: 组装快照 ═══
        uint64_t timestamp = static_cast<uint64_t>(cycle) * 20; // 模拟 20ms 轮询间隔
        auto snapshot = poller.assemble(coilResponses, wordResponses, timestamp);

        // ═══ Step 4: 更新快照并读取反馈 ═══
        device.updateSnapshot(std::move(snapshot));

        if (device.isStateTrusted()) {
            ++trustedCycles;

            // 读取反馈寄存器，验证解码不抛异常
            const RegisterInfo* pPos = registry.findByAddress(RegisterArea::HoldingReg, 0x0100);
            ASSERT_NE(pPos, nullptr);
            PlcValue posVal = device.readValue(*pPos);
            EXPECT_NO_THROW({
                auto v = getValue<int16_t>(posVal);
                (void)v;
            });

            const RegisterInfo* pReady = registry.findByAddress(RegisterArea::Coil, 0x0000);
            ASSERT_NE(pReady, nullptr);
            PlcValue readyVal = device.readValue(*pReady);
            EXPECT_NO_THROW({
                auto b = getValue<bool>(readyVal);
                (void)b;
            });
        }

        // ═══ Step 5: 写入命令（模拟发送运动指令） ═══
        const RegisterInfo* pCmdEnable = registry.findByAddress(RegisterArea::Coil, 0x0010);
        ASSERT_NE(pCmdEnable, nullptr);

        // 首次 cycle 发送 enable 脉冲
        if (cycle == 0) {
            auto writeResult = device.writeBool(*pCmdEnable, true);
            if (writeResult.ok()) {
                ++writeSuccess;
            }
        }

        // 每个 cycle 更新目标位置
        const RegisterInfo* pCmdPos = registry.findByAddress(RegisterArea::HoldingReg, 0x0200);
        ASSERT_NE(pCmdPos, nullptr);
        auto posResult = device.writeInt16(*pCmdPos, static_cast<int16_t>(100 + cycle));
        if (posResult.ok()) {
            ++writeSuccess;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_GT(trustedCycles, 0) << "At least some polling cycles must produce trusted snapshots";
    EXPECT_GT(writeSuccess, 0) << "At least some write operations must succeed";

    client->stop();
}

// ============================================================================
// 10. 功能码覆盖矩阵验证（FC01/FC03/FC05/FC06/FC10 全部命中）
// ============================================================================

TEST_F(ModbusTcpIntegrationTest, FunctionCodeCoverage_AllFiveFunctionCodes) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行
    //
    // 发送所有 5 种 Modbus 功能码，验证 diagslave 正常响应

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected())
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    // FC01: Read Coils
    {
        std::vector<uint8_t> payload;
        auto result = client->readCoils(0x0000, 8, payload);
        EXPECT_TRUE(result.ok()) << "FC01 Read Coils: " << result.diagnostic;
        if (result.ok()) {
            EXPECT_FALSE(payload.empty()) << "FC01 should return non-empty payload";
        }
    }

    // FC03: Read Holding Registers
    {
        std::vector<uint16_t> payload;
        auto result = client->readHoldingRegisters(0x0000, 2, payload);
        EXPECT_TRUE(result.ok()) << "FC03 Read Holding Reg: " << result.diagnostic;
        if (result.ok()) {
            EXPECT_FALSE(payload.empty()) << "FC03 should return non-empty payload";
        }
    }

    // FC05: Write Single Coil
    {
        auto result = client->writeSingleCoil(0x000A, true);
        EXPECT_TRUE(result.ok()) << "FC05 Write Single Coil: " << result.diagnostic;
    }

    // FC06: Write Single Register
    {
        auto result = client->writeSingleRegister(0x00C8, 0xABCD);
        EXPECT_TRUE(result.ok()) << "FC06 Write Single Reg: " << result.diagnostic;
    }

    // FC10: Write Multiple Registers (Float32 = 2 registers)
    {
        std::vector<uint16_t> values = {0x1234, 0x5678};
        auto result = client->writeMultipleRegisters(0x0190, values);
        EXPECT_TRUE(result.ok()) << "FC10 Write Multiple Reg: " << result.diagnostic;
    }

    // 最终回读验证 FC06 写入值
    {
        std::vector<uint16_t> payload;
        auto result = client->readHoldingRegisters(0x00C8, 1, payload);
        ASSERT_TRUE(result.ok());
        if (!payload.empty()) {
            EXPECT_EQ(payload[0], 0xABCD) << "FC06 value should persist";
        }
    }

    client->stop();
}

// ============================================================================
// 11. 端到端 IO 通道闭环（Write → Read 验证 diagslave 回显）
// ============================================================================

TEST_F(ModbusTcpIntegrationTest, EndToEnd_WriteThenRead_ClosedLoop) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行
    //
    // 验证写入的值可以通过读操作立即回读（diagslave 回显特性）

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected())
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    // Coil 闭环: 写 → 读
    {
        // 先写
        auto wResult = client->writeSingleCoil(0x0020, true);
        ASSERT_TRUE(wResult.ok()) << "Write coil: " << wResult.diagnostic;

        // 立即回读
        std::vector<uint8_t> rPayload;
        auto rResult = client->readCoils(0x0020, 1, rPayload);
        ASSERT_TRUE(rResult.ok()) << "Readback coil: " << rResult.diagnostic;

        if (!rPayload.empty()) {
            bool coilState = (rPayload[0] & 0x01) != 0;
            EXPECT_TRUE(coilState) << "Coil 0x20 should be ON after write";
        }
    }

    // Register 闭环: FC06 → FC03
    {
        auto wResult = client->writeSingleRegister(0x01F4, 0x3E7);
        ASSERT_TRUE(wResult.ok()) << "Write register: " << wResult.diagnostic;

        std::vector<uint16_t> rPayload;
        auto rResult = client->readHoldingRegisters(0x01F4, 1, rPayload);
        ASSERT_TRUE(rResult.ok()) << "Readback register: " << rResult.diagnostic;

        if (!rPayload.empty()) {
            EXPECT_EQ(rPayload[0], 0x3E7)
                << "Register 500 should echo written value 999 (0x3E7)";
        }
    }

    // Multi-register 闭环: FC10 → FC03
    {
        std::vector<uint16_t> values = {0x1111, 0x2222, 0x3333};
        auto wResult = client->writeMultipleRegisters(0x0258, values);
        ASSERT_TRUE(wResult.ok()) << "Write multi register: " << wResult.diagnostic;

        std::vector<uint16_t> rPayload;
        auto rResult = client->readHoldingRegisters(0x0258, 3, rPayload);
        ASSERT_TRUE(rResult.ok()) << "Readback multi: " << rResult.diagnostic;

        ASSERT_EQ(rPayload.size(), 3) << "Should read back 3 registers";
        if (rPayload.size() == 3) {
            EXPECT_EQ(rPayload[0], 0x1111);
            EXPECT_EQ(rPayload[1], 0x2222);
            EXPECT_EQ(rPayload[2], 0x3333);
        }
    }

    client->stop();
}

// ============================================================================
// 12. PlcDevice → AsioModbusTcpClient 全栈写映射验证
// ============================================================================

TEST_F(ModbusTcpIntegrationTest, PlcDevice_WriteThroughClient_FullStackMapping) {
    // 前置条件: diagslave -m tcp -p 502 -a 1 正在运行
    //
    // 验证 PlcDevice 的 writeBool / writeInt16 / writeFloat 正确映射到
    // FC05 / FC06 / FC10 协议函数，且 diagslave 正确接收

    ProtocolProfile profile;
    PlcDevice device(profile);

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected())
        << "diagslave must be running: diagslave -m tcp -p 502 -a 1";

    device.bindTransport(client.get());

    // writeBool → FC05
    {
        RegisterInfo coilReg = makeCoil(0x0030, RegisterAccess::ReadWrite,
                                         RegisterBehavior::AutoResetEdgeTrigger,
                                         RegisterGroup::Command, "cmd_coil_48");
        auto result = device.writeBool(coilReg, true);
        EXPECT_TRUE(result.ok()) << "writeBool → FC05: " << result.diagnostic;

        std::vector<uint8_t> rPayload;
        auto rResult = client->readCoils(0x0030, 1, rPayload);
        ASSERT_TRUE(rResult.ok());
        if (!rPayload.empty()) {
            bool state = (rPayload[0] & 0x01) != 0;
            EXPECT_TRUE(state) << "Coil should be ON after writeBool";
        }
    }

    // writeInt16 → FC06
    {
        RegisterInfo int16Reg = makeHoldingReg(0x0320, RegisterType::Int16,
                                                RegisterAccess::ReadWrite,
                                                RegisterBehavior::AutoResetEdgeTrigger,
                                                RegisterGroup::Command, "cmd_int16_800");
        auto result = device.writeInt16(int16Reg, -12345);
        EXPECT_TRUE(result.ok()) << "writeInt16 → FC06: " << result.diagnostic;

        std::vector<uint16_t> rPayload;
        auto rResult = client->readHoldingRegisters(0x0320, 1, rPayload);
        ASSERT_TRUE(rResult.ok());
        if (!rPayload.empty()) {
            EXPECT_EQ(static_cast<int16_t>(rPayload[0]), -12345)
                << "Int16 register should echo written value";
        }
    }

    // writeFloat → FC10 (2 registers)
    {
        RegisterInfo floatReg = makeHoldingReg(0x03E8, RegisterType::Float32,
                                                RegisterAccess::ReadWrite,
                                                RegisterBehavior::AutoResetEdgeTrigger,
                                                RegisterGroup::Command, "cmd_float_1000");
        auto result = device.writeFloat(floatReg, -99.5f);
        EXPECT_TRUE(result.ok()) << "writeFloat → FC10: " << result.diagnostic;

        std::vector<uint16_t> rPayload;
        auto rResult = client->readHoldingRegisters(0x03E8, 2, rPayload);
        ASSERT_TRUE(rResult.ok()) << "FC10 write should be readable";
        if (rResult.ok()) {
            EXPECT_EQ(rPayload.size(), 2)
                << "Float32 should occupy 2 registers";
            // 验证写入成功：两个寄存器不全为 0（具体值依赖 endian）
            bool nonZero = (rPayload[0] != 0 || rPayload[1] != 0);
            EXPECT_TRUE(nonZero) << "Float32 registers should be non-zero";
        }
    }

    client->stop();
}