// tests/infrastructure/protocol/test_real_feedback_readonly.cpp
// 真实物理 PLC 反馈寄存器只读测试
//
// 测试范围:
//   - 读取 X/Y/Z 三轴所有 RegisterGroup::Feedback 类型寄存器（共 35 个）
//   - 排除 R 轴所有寄存器
//   - 排除所有 RegisterGroup::Command 类型寄存器
//   - 只读操作，不进行任何写操作，确保物理设备安全
//
// 前置条件:
//   真实汇川 PLC 通过 Modbus TCP 连接，默认 192.168.1.100:502
//   （可根据实际环境修改 config_ 中的 IP/端口）
//
// 依赖:
//   - AsioModbusTcpClient (L3 传输层)
//   - PlcPoller             (L2 协议运行时 — 轮询)
//   - PlcDevice             (L1 驱动集成层 — 快照读取)
//   - RegisterRegistry      (寄存器声明)
//   - ProtocolProfile       (端序策略)
//   - RegisterAddressAll.h  (真实 PLC 寄存器地址映射表)

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdint>
#include <memory>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdio>

#include "infrastructure/plc/protocol/AsioModbusTcpClient.h"
#include "infrastructure/plc/protocol/PlcPoller.h"
#include "infrastructure/plc/protocol/PlcDevice.h"
#include "infrastructure/plc/protocol/RegisterRegistry.h"
#include "infrastructure/plc/protocol/ProtocolProfile.h"
#include "infrastructure/plc/protocol/RegisterMetadata.h"
#include "infrastructure/plc/protocol/RegisterAddressAll.h"
#include "infrastructure/ISystemDriver.h"

using namespace plc::protocol;

// ============================================================================
// 测试 Fixture：真实 PLC 反馈寄存器只读测试
// ============================================================================

class RealFeedbackReadTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 默认连接真实 PLC（汇川），根据实际环境修改 IP
        config_.host = "192.168.1.88";
        config_.port = 502;
        config_.unitId = 0x01;
        config_.timeoutMs = 2000;
        config_.reconnectIntervalMs = 1000;
    }

    void TearDown() override {
        // 各测试自行管理 client 生命周期
    }

    /// @brief 创建并启动客户端，等待连接建立
    std::unique_ptr<AsioModbusTcpClient> createAndStart() {
        auto client = std::make_unique<AsioModbusTcpClient>(config_);
        client->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        return client;
    }

    /// @brief 等待连接建立（带超时断言）
    void waitForConnection(AsioModbusTcpClient& client, int maxWaitMs = 3000) {
        auto start = std::chrono::steady_clock::now();
        while (!client.isConnected()) {
            if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(maxWaitMs)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    /// @brief 安全读取 Float32，NaN 也视为有效读取
    static bool safeReadFloat(PlcDevice& device, const RegisterInfo& reg, float& out) {
        try {
            PlcValue val = device.readValue(reg);
            out = getValue<float>(val);
            return true;
        } catch (...) {
            return false;
        }
    }

    /// @brief 安全读取 Int16
    static bool safeReadInt16(PlcDevice& device, const RegisterInfo& reg, int16_t& out) {
        try {
            PlcValue val = device.readValue(reg);
            out = getValue<int16_t>(val);
            return true;
        } catch (...) {
            return false;
        }
    }

    /// @brief 安全读取 Bool
    static bool safeReadBool(PlcDevice& device, const RegisterInfo& reg, bool& out) {
        try {
            PlcValue val = device.readValue(reg);
            out = getValue<bool>(val);
            return true;
        } catch (...) {
            return false;
        }
    }

    AsioModbusTcpClient::Config config_;
};

// ============================================================================
// 辅助：构建非 R 轴 Feedback 寄存器的 RegisterRegistry
// ============================================================================

/// @brief 将所有非 R 轴、RegisterGroup::Feedback 的寄存器注册到 registry
/// @return 注册表，包含 X/Y/Z 三轴所有 Feedback 寄存器
static RegisterRegistry buildFeedbackOnlyRegistry() {
    RegisterRegistry registry;

    // ==================== X 轴 Feedback (14 个) ====================
    registry.addAll({
        // Coil 反馈 (5 个)
        plc::reg::x_axis::feedback::MOVE_DONE,
        plc::reg::x_axis::feedback::ABS_MOVING,
        plc::reg::x_axis::feedback::REL_MOVING,
        plc::reg::x_axis::feedback::JOGGING,
        plc::reg::x_axis::feedback::LINKAGE_STATE,
        // HoldingReg 反馈 (9 个)
        plc::reg::x_axis::feedback::REL_POSITION_OLD,
        plc::reg::x_axis::feedback::ABS_POSITION_OLD,
        plc::reg::x_axis::feedback::STATE,
        plc::reg::x_axis::feedback::ABS_POSITION,
        plc::reg::x_axis::feedback::REL_POSITION,
        plc::reg::x_axis::feedback::REL_ZERO_OFFSET,
        plc::reg::x_axis::feedback::X1_CURRENT_POS,
        plc::reg::x_axis::feedback::X2_CURRENT_POS,
        plc::reg::x_axis::feedback::REL_ZERO_RECORD,
    });

    // ==================== Y 轴 Feedback (11 个) ====================
    registry.addAll({
        // Coil 反馈 (4 个)
        plc::reg::y_axis::feedback::MOVE_DONE,
        plc::reg::y_axis::feedback::ABS_MOVING,
        plc::reg::y_axis::feedback::REL_MOVING,
        plc::reg::y_axis::feedback::JOGGING,
        // HoldingReg 反馈 (7 个)
        plc::reg::y_axis::feedback::REL_POSITION_OLD,
        plc::reg::y_axis::feedback::ABS_POSITION_OLD,
        plc::reg::y_axis::feedback::STATE,
        plc::reg::y_axis::feedback::ABS_POSITION,
        plc::reg::y_axis::feedback::REL_POSITION,
        plc::reg::y_axis::feedback::REL_ZERO_OFFSET,
        plc::reg::y_axis::feedback::REL_ZERO_RECORD,
    });

    // ==================== Z 轴 Feedback (10 个) ====================
    registry.addAll({
        // Coil 反馈 (4 个)
        plc::reg::z_axis::feedback::MOVE_DONE,
        plc::reg::z_axis::feedback::ABS_MOVING,
        plc::reg::z_axis::feedback::REL_MOVING,
        plc::reg::z_axis::feedback::JOGGING,
        // HoldingReg 反馈 (6 个)
        plc::reg::z_axis::feedback::REL_POSITION_OLD,
        plc::reg::z_axis::feedback::ABS_POSITION_OLD,
        plc::reg::z_axis::feedback::STATE,
        plc::reg::z_axis::feedback::ABS_POSITION,
        plc::reg::z_axis::feedback::REL_POSITION,
        plc::reg::z_axis::feedback::REL_ZERO_OFFSET,
        plc::reg::z_axis::feedback::REL_ZERO_RECORD,
    });

    return registry;
}

// ============================================================================
// 测试 1: 验证寄存器筛选 — 仅包含非R轴 Feedback
// ============================================================================

TEST_F(RealFeedbackReadTest, Registry_OnlyNonRAxisFeedback) {
    auto registry = buildFeedbackOnlyRegistry();

    // 验证不含 R 轴寄存器
    const std::vector<uint16_t> rAxisCoilAddrs = {103, 119, 120, 121};
    const std::vector<uint16_t> rAxisRegAddrs = {6, 16, 103, 113, 132, 134, 142, 1026};

    for (uint16_t addr : rAxisCoilAddrs) {
        EXPECT_EQ(registry.findByAddress(RegisterArea::Coil, addr), nullptr)
            << "R-axis Coil " << addr << " should NOT be in registry";
    }
    for (uint16_t addr : rAxisRegAddrs) {
        EXPECT_EQ(registry.findByAddress(RegisterArea::HoldingReg, addr), nullptr)
            << "R-axis HoldingReg " << addr << " should NOT be in registry";
    }

    // 验证包含 X 轴 Feedback 寄存器
    EXPECT_NE(registry.findByAddress(RegisterArea::Coil, 100), nullptr);
    EXPECT_NE(registry.findByAddress(RegisterArea::HoldingReg, 120), nullptr);
    EXPECT_NE(registry.findByAddress(RegisterArea::HoldingReg, 170), nullptr);

    // 验证不在 Command 寄存器（X 轴）
    EXPECT_EQ(registry.findByAddress(RegisterArea::Coil, 0), nullptr);
    EXPECT_EQ(registry.findByAddress(RegisterArea::Coil, 40), nullptr);
    EXPECT_EQ(registry.findByAddress(RegisterArea::HoldingReg, 20), nullptr);

    // 验证不含 Alarm 寄存器
    EXPECT_EQ(registry.findByAddress(RegisterArea::HoldingReg, 110), nullptr);
    EXPECT_EQ(registry.findByAddress(RegisterArea::Coil, 123), nullptr);
}

// ============================================================================
// 测试 2: 完整读取管线 — 真实 PLC 连接
// ============================================================================

TEST_F(RealFeedbackReadTest, FullPipeline_ReadAllFeedback_FromRealPLC) {
    auto registry = buildFeedbackOnlyRegistry();
    PlcPoller poller(registry);
    const auto& profile = INOVANCE_PROFILE; // BigEndian + LowWordFirst

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected())
        << "Real PLC must be reachable at " << config_.host << ":" << config_.port;

    // 1. prepare()
    auto req = poller.prepare();
    EXPECT_FALSE(req.coilRequests.empty());
    EXPECT_FALSE(req.wordRequests.empty());

    // 2. FC01
    std::vector<std::vector<uint8_t>> coilResponses;
    int coilRequestsFail = 0;
    std::ostringstream coilErrors;
    for (const auto& cr : req.coilRequests) {
        std::vector<uint8_t> payload;
        auto result = client->readCoils(cr.range.startAddress, cr.range.count, payload);
        if (result.ok()) {
            coilResponses.push_back(std::move(payload));
        } else {
            ++coilRequestsFail;
            coilErrors << "  FC01 [" << cr.range.startAddress
                       << "," << cr.range.count << "]: "
                       << result.diagnostic << "\n";
        }
    }
    EXPECT_EQ(coilRequestsFail, 0)
        << "All FC01 reads must succeed.\nErrors:\n" << coilErrors.str();

    // 3. FC03
    std::vector<std::vector<uint16_t>> wordResponses;
    int wordRequestsFail = 0;
    std::ostringstream wordErrors;
    for (const auto& wr : req.wordRequests) {
        std::vector<uint16_t> payload;
        auto result = client->readHoldingRegisters(
            wr.range.startAddress, wr.range.count, payload);
        if (result.ok()) {
            wordResponses.push_back(std::move(payload));
        } else {
            ++wordRequestsFail;
            wordErrors << "  FC03 [" << wr.range.startAddress
                       << "," << wr.range.count << "]: "
                       << result.diagnostic << "\n";
        }
    }
    EXPECT_EQ(wordRequestsFail, 0)
        << "All FC03 reads must succeed.\nErrors:\n" << wordErrors.str();

    // 4. assemble()
    auto snapshot = poller.assemble(coilResponses, wordResponses, 12345);
    EXPECT_TRUE(snapshot.isTrusted());
    EXPECT_EQ(snapshot.timestamp, 12345);

    // 5. PlcDevice
    PlcDevice device(profile);
    device.updateSnapshot(std::move(snapshot));
    EXPECT_TRUE(device.isStateTrusted());

    // ==================== 结构化打印 ====================
    // Helper: 从快照获取 Int16 原始 hex
    auto getInt16Raw = [&](const RegisterInfo& reg) -> std::string {
        auto raw = device.snapshot().words.getWords(reg.address, 1);
        if (raw.has_value() && raw->size() >= 1) {
            char buf[16];
            snprintf(buf, sizeof(buf), "0x%04X", (*raw)[0]);
            return buf;
        }
        return "N/A";
    };

    // Helper: 从快照获取 Float32 两个寄存器的原始 hex
    auto getFloat32Raw = [&](const RegisterInfo& reg) -> std::string {
        auto raw = device.snapshot().words.getWords(reg.address, 2);
        if (raw.has_value() && raw->size() >= 2) {
            char buf[32];
            snprintf(buf, sizeof(buf), "0x%04X @%u, 0x%04X @%u",
                     (*raw)[0], reg.address, (*raw)[1], reg.address + 1);
            return buf;
        }
        return "N/A";
    };

    auto printFeedback = [&](const char* axisName,
                             const RegisterInfo& moveDone,
                             const RegisterInfo& absMoving,
                             const RegisterInfo& relMoving,
                             const RegisterInfo& jogging,
                             const RegisterInfo* linkageOrNull,
                             const RegisterInfo& state,
                             const RegisterInfo& absPos,
                             const RegisterInfo& relPos,
                             const RegisterInfo& relZeroOffset,
                             const RegisterInfo& relZeroRecord,
                             const RegisterInfo& relPosOld,
                             const RegisterInfo& absPosOld,
                             const RegisterInfo* x1PosOrNull,
                             const RegisterInfo* x2PosOrNull)
    {
      printf("\n--- %s Feedback ---\n", axisName);

      // Coils
      auto printCoil = [&](const RegisterInfo& reg, const char* name) {
        bool v;
        if (safeReadBool(device, reg, v)) {
          printf("  Coil %-4u %-16s = %d\n", reg.address, name, v);
        } else {
          printf("  Coil %-4u %-16s = ERR\n", reg.address, name);
        }
      };
      printCoil(moveDone, "MOVE_DONE");
      printCoil(absMoving, "ABS_MOVING");
      printCoil(relMoving, "REL_MOVING");
      printCoil(jogging, "JOGGING");
      if (linkageOrNull) printCoil(*linkageOrNull, "LINKAGE_STATE");

      // Int16 STATE
      int16_t stateVal;
      if (safeReadInt16(device, state, stateVal)) {
        printf("  HR   %-4u %-16s = %-8d [raw: %s]\n",
               state.address, "STATE", stateVal,
               getInt16Raw(state).c_str());
      } else {
        printf("  HR   %-4u %-16s = ERR\n", state.address, "STATE");
      }

      // Float32 (均带原始 hex)
      auto printFloatReg = [&](const RegisterInfo& reg, const char* name) {
        float v;
        if (safeReadFloat(device, reg, v)) {
          printf("  HR   %-4u %-16s = %-12.3f [raw: %s]\n",
                 reg.address, name, v,
                 getFloat32Raw(reg).c_str());
        } else {
          printf("  HR   %-4u %-16s = ERR\n", reg.address, name);
        }
      };

      printFloatReg(absPos, "ABS_POSITION");
      printFloatReg(relPos, "REL_POSITION");
      printFloatReg(relZeroOffset, "REL_ZERO_OFFSET");
      printFloatReg(relZeroRecord, "REL_ZERO_RECORD");
      printFloatReg(relPosOld, "REL_POSITION_OLD");
      printFloatReg(absPosOld, "ABS_POSITION_OLD");
      if (x1PosOrNull) printFloatReg(*x1PosOrNull, "X1_CURRENT_POS");
      if (x2PosOrNull) printFloatReg(*x2PosOrNull, "X2_CURRENT_POS");
    };

    printf("\n======================================================================\n");
    printf("  PLC Feedback — hex raw + decoded values (INOVANCE_PROFILE: LowWordFirst)\n");
    printf("======================================================================\n");

    // X
    printFeedback("X",
      plc::reg::x_axis::feedback::MOVE_DONE,
      plc::reg::x_axis::feedback::ABS_MOVING,
      plc::reg::x_axis::feedback::REL_MOVING,
      plc::reg::x_axis::feedback::JOGGING,
      &plc::reg::x_axis::feedback::LINKAGE_STATE,
      plc::reg::x_axis::feedback::STATE,
      plc::reg::x_axis::feedback::ABS_POSITION,
      plc::reg::x_axis::feedback::REL_POSITION,
      plc::reg::x_axis::feedback::REL_ZERO_OFFSET,
      plc::reg::x_axis::feedback::REL_ZERO_RECORD,
      plc::reg::x_axis::feedback::REL_POSITION_OLD,
      plc::reg::x_axis::feedback::ABS_POSITION_OLD,
      &plc::reg::x_axis::feedback::X1_CURRENT_POS,
      &plc::reg::x_axis::feedback::X2_CURRENT_POS);

    // Y
    printFeedback("Y",
      plc::reg::y_axis::feedback::MOVE_DONE,
      plc::reg::y_axis::feedback::ABS_MOVING,
      plc::reg::y_axis::feedback::REL_MOVING,
      plc::reg::y_axis::feedback::JOGGING,
      nullptr,
      plc::reg::y_axis::feedback::STATE,
      plc::reg::y_axis::feedback::ABS_POSITION,
      plc::reg::y_axis::feedback::REL_POSITION,
      plc::reg::y_axis::feedback::REL_ZERO_OFFSET,
      plc::reg::y_axis::feedback::REL_ZERO_RECORD,
      plc::reg::y_axis::feedback::REL_POSITION_OLD,
      plc::reg::y_axis::feedback::ABS_POSITION_OLD,
      nullptr, nullptr);

    // Z
    printFeedback("Z",
      plc::reg::z_axis::feedback::MOVE_DONE,
      plc::reg::z_axis::feedback::ABS_MOVING,
      plc::reg::z_axis::feedback::REL_MOVING,
      plc::reg::z_axis::feedback::JOGGING,
      nullptr,
      plc::reg::z_axis::feedback::STATE,
      plc::reg::z_axis::feedback::ABS_POSITION,
      plc::reg::z_axis::feedback::REL_POSITION,
      plc::reg::z_axis::feedback::REL_ZERO_OFFSET,
      plc::reg::z_axis::feedback::REL_ZERO_RECORD,
      plc::reg::z_axis::feedback::REL_POSITION_OLD,
      plc::reg::z_axis::feedback::ABS_POSITION_OLD,
      nullptr, nullptr);

    printf("======================================================================\n\n");

    // ==================== ASSERT 验证 ====================
    {
        bool moveDone, absMoving, relMoving, jogging, linkageState;
        EXPECT_TRUE(safeReadBool(device, plc::reg::x_axis::feedback::MOVE_DONE, moveDone));
        EXPECT_TRUE(safeReadBool(device, plc::reg::x_axis::feedback::ABS_MOVING, absMoving));
        EXPECT_TRUE(safeReadBool(device, plc::reg::x_axis::feedback::REL_MOVING, relMoving));
        EXPECT_TRUE(safeReadBool(device, plc::reg::x_axis::feedback::JOGGING, jogging));
        EXPECT_TRUE(safeReadBool(device, plc::reg::x_axis::feedback::LINKAGE_STATE, linkageState));
    }
    {
        float v;
        EXPECT_TRUE(safeReadFloat(device, plc::reg::x_axis::feedback::ABS_POSITION, v));
        EXPECT_FALSE(std::isnan(v)) << "X absPos NaN";
        EXPECT_FALSE(std::isinf(v)) << "X absPos Inf";
        EXPECT_TRUE(safeReadFloat(device, plc::reg::x_axis::feedback::REL_POSITION, v));
        EXPECT_FALSE(std::isnan(v)) << "X relPos NaN";
        EXPECT_FALSE(std::isinf(v)) << "X relPos Inf";
    }
    {
        float v;
        EXPECT_TRUE(safeReadFloat(device, plc::reg::y_axis::feedback::ABS_POSITION, v));
        EXPECT_FALSE(std::isnan(v));
        EXPECT_FALSE(std::isinf(v));
    }
    {
        float v;
        EXPECT_TRUE(safeReadFloat(device, plc::reg::z_axis::feedback::ABS_POSITION, v));
        EXPECT_FALSE(std::isnan(v));
        EXPECT_FALSE(std::isinf(v));
    }

    client->stop();
}

// ============================================================================
// 测试 3: 连续轮询稳定性 — 10 个周期
// ============================================================================

TEST_F(RealFeedbackReadTest, ContinuousPolling_10Cycles_Stable) {
    auto registry = buildFeedbackOnlyRegistry();
    PlcPoller poller(registry);
    const auto& profile = INOVANCE_PROFILE;
    PlcDevice device(profile);

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected());

    int cyclesOk = 0;
    int cyclesFail = 0;

    for (int cycle = 0; cycle < 10; ++cycle) {
        auto req = poller.prepare();

        std::vector<std::vector<uint8_t>> coilResponses;
        bool allOk = true;
        for (const auto& cr : req.coilRequests) {
            std::vector<uint8_t> payload;
            auto result = client->readCoils(cr.range.startAddress, cr.range.count, payload);
            if (result.ok()) {
                coilResponses.push_back(std::move(payload));
            } else { allOk = false; break; }
        }

        std::vector<std::vector<uint16_t>> wordResponses;
        if (allOk) {
            for (const auto& wr : req.wordRequests) {
                std::vector<uint16_t> payload;
                auto result = client->readHoldingRegisters(wr.range.startAddress, wr.range.count, payload);
                if (result.ok()) {
                    wordResponses.push_back(std::move(payload));
                } else { allOk = false; break; }
            }
        }

        if (allOk) {
            auto snap = poller.assemble(coilResponses, wordResponses, static_cast<uint64_t>(cycle) * 20);
            if (snap.isTrusted()) {
                device.updateSnapshot(std::move(snap));
                ++cyclesOk;
            } else {
                ++cyclesFail;
            }
        } else {
            ++cyclesFail;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    EXPECT_EQ(cyclesFail, 0);
    EXPECT_EQ(cyclesOk, 10);
    EXPECT_TRUE(device.isStateTrusted());

    client->stop();
}

// ============================================================================
// 测试 4: 各轴 INDEPENDENT 读取
// ============================================================================

TEST_F(RealFeedbackReadTest, AxisIndependent_XAxis_AllFeedbackReadable) {
    RegisterRegistry registry;
    registry.addAll({
        plc::reg::x_axis::feedback::MOVE_DONE,
        plc::reg::x_axis::feedback::ABS_MOVING,
        plc::reg::x_axis::feedback::REL_MOVING,
        plc::reg::x_axis::feedback::JOGGING,
        plc::reg::x_axis::feedback::LINKAGE_STATE,
        plc::reg::x_axis::feedback::STATE,
        plc::reg::x_axis::feedback::ABS_POSITION,
        plc::reg::x_axis::feedback::REL_POSITION,
        plc::reg::x_axis::feedback::REL_ZERO_OFFSET,
        plc::reg::x_axis::feedback::X1_CURRENT_POS,
        plc::reg::x_axis::feedback::X2_CURRENT_POS,
        plc::reg::x_axis::feedback::REL_POSITION_OLD,
        plc::reg::x_axis::feedback::ABS_POSITION_OLD,
        plc::reg::x_axis::feedback::REL_ZERO_RECORD,
    });

    PlcPoller poller(registry);
    const auto& profile = INOVANCE_PROFILE;

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected());

    auto req = poller.prepare();

    std::vector<std::vector<uint8_t>> coilResponses;
    for (const auto& cr : req.coilRequests) {
        std::vector<uint8_t> payload;
        auto result = client->readCoils(cr.range.startAddress, cr.range.count, payload);
        ASSERT_TRUE(result.ok()) << "X coil: " << result.diagnostic;
        coilResponses.push_back(std::move(payload));
    }

    std::vector<std::vector<uint16_t>> wordResponses;
    for (const auto& wr : req.wordRequests) {
        std::vector<uint16_t> payload;
        auto result = client->readHoldingRegisters(wr.range.startAddress, wr.range.count, payload);
        ASSERT_TRUE(result.ok()) << "X word: " << result.diagnostic;
        wordResponses.push_back(std::move(payload));
    }

    auto snapshot = poller.assemble(coilResponses, wordResponses, 0);
    ASSERT_TRUE(snapshot.isTrusted());

    PlcDevice device(profile);
    device.updateSnapshot(std::move(snapshot));

    bool vBool; float vFloat; int16_t vInt16;
    EXPECT_TRUE(safeReadBool(device, plc::reg::x_axis::feedback::MOVE_DONE, vBool));
    EXPECT_TRUE(safeReadBool(device, plc::reg::x_axis::feedback::ABS_MOVING, vBool));
    EXPECT_TRUE(safeReadBool(device, plc::reg::x_axis::feedback::REL_MOVING, vBool));
    EXPECT_TRUE(safeReadBool(device, plc::reg::x_axis::feedback::JOGGING, vBool));
    EXPECT_TRUE(safeReadBool(device, plc::reg::x_axis::feedback::LINKAGE_STATE, vBool));
    EXPECT_TRUE(safeReadInt16(device, plc::reg::x_axis::feedback::STATE, vInt16));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::x_axis::feedback::ABS_POSITION, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::x_axis::feedback::REL_POSITION, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::x_axis::feedback::REL_ZERO_OFFSET, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::x_axis::feedback::X1_CURRENT_POS, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::x_axis::feedback::X2_CURRENT_POS, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::x_axis::feedback::REL_POSITION_OLD, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::x_axis::feedback::ABS_POSITION_OLD, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::x_axis::feedback::REL_ZERO_RECORD, vFloat));

    client->stop();
}

TEST_F(RealFeedbackReadTest, AxisIndependent_YAxis_AllFeedbackReadable) {
    RegisterRegistry registry;
    registry.addAll({
        plc::reg::y_axis::feedback::MOVE_DONE,
        plc::reg::y_axis::feedback::ABS_MOVING,
        plc::reg::y_axis::feedback::REL_MOVING,
        plc::reg::y_axis::feedback::JOGGING,
        plc::reg::y_axis::feedback::STATE,
        plc::reg::y_axis::feedback::ABS_POSITION,
        plc::reg::y_axis::feedback::REL_POSITION,
        plc::reg::y_axis::feedback::REL_ZERO_OFFSET,
        plc::reg::y_axis::feedback::REL_POSITION_OLD,
        plc::reg::y_axis::feedback::ABS_POSITION_OLD,
        plc::reg::y_axis::feedback::REL_ZERO_RECORD,
    });

    PlcPoller poller(registry);
    const auto& profile = INOVANCE_PROFILE;

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected());

    auto req = poller.prepare();

    std::vector<std::vector<uint8_t>> coilResponses;
    for (const auto& cr : req.coilRequests) {
        std::vector<uint8_t> payload;
        auto result = client->readCoils(cr.range.startAddress, cr.range.count, payload);
        ASSERT_TRUE(result.ok()) << "Y coil: " << result.diagnostic;
        coilResponses.push_back(std::move(payload));
    }

    std::vector<std::vector<uint16_t>> wordResponses;
    for (const auto& wr : req.wordRequests) {
        std::vector<uint16_t> payload;
        auto result = client->readHoldingRegisters(wr.range.startAddress, wr.range.count, payload);
        ASSERT_TRUE(result.ok()) << "Y word: " << result.diagnostic;
        wordResponses.push_back(std::move(payload));
    }

    auto snapshot = poller.assemble(coilResponses, wordResponses, 0);
    ASSERT_TRUE(snapshot.isTrusted());

    PlcDevice device(profile);
    device.updateSnapshot(std::move(snapshot));

    bool vBool; float vFloat; int16_t vInt16;
    EXPECT_TRUE(safeReadBool(device, plc::reg::y_axis::feedback::MOVE_DONE, vBool));
    EXPECT_TRUE(safeReadBool(device, plc::reg::y_axis::feedback::ABS_MOVING, vBool));
    EXPECT_TRUE(safeReadBool(device, plc::reg::y_axis::feedback::REL_MOVING, vBool));
    EXPECT_TRUE(safeReadBool(device, plc::reg::y_axis::feedback::JOGGING, vBool));
    EXPECT_TRUE(safeReadInt16(device, plc::reg::y_axis::feedback::STATE, vInt16));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::y_axis::feedback::ABS_POSITION, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::y_axis::feedback::REL_POSITION, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::y_axis::feedback::REL_ZERO_OFFSET, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::y_axis::feedback::REL_POSITION_OLD, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::y_axis::feedback::ABS_POSITION_OLD, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::y_axis::feedback::REL_ZERO_RECORD, vFloat));

    client->stop();
}

TEST_F(RealFeedbackReadTest, AxisIndependent_ZAxis_AllFeedbackReadable) {
    RegisterRegistry registry;
    registry.addAll({
        plc::reg::z_axis::feedback::MOVE_DONE,
        plc::reg::z_axis::feedback::ABS_MOVING,
        plc::reg::z_axis::feedback::REL_MOVING,
        plc::reg::z_axis::feedback::JOGGING,
        plc::reg::z_axis::feedback::STATE,
        plc::reg::z_axis::feedback::ABS_POSITION,
        plc::reg::z_axis::feedback::REL_POSITION,
        plc::reg::z_axis::feedback::REL_ZERO_OFFSET,
        plc::reg::z_axis::feedback::REL_POSITION_OLD,
        plc::reg::z_axis::feedback::ABS_POSITION_OLD,
        plc::reg::z_axis::feedback::REL_ZERO_RECORD,
    });

    PlcPoller poller(registry);
    const auto& profile = INOVANCE_PROFILE;

    auto client = createAndStart();
    ASSERT_TRUE(client->isConnected());

    auto req = poller.prepare();

    std::vector<std::vector<uint8_t>> coilResponses;
    for (const auto& cr : req.coilRequests) {
        std::vector<uint8_t> payload;
        auto result = client->readCoils(cr.range.startAddress, cr.range.count, payload);
        ASSERT_TRUE(result.ok()) << "Z coil: " << result.diagnostic;
        coilResponses.push_back(std::move(payload));
    }

    std::vector<std::vector<uint16_t>> wordResponses;
    for (const auto& wr : req.wordRequests) {
        std::vector<uint16_t> payload;
        auto result = client->readHoldingRegisters(wr.range.startAddress, wr.range.count, payload);
        ASSERT_TRUE(result.ok()) << "Z word: " << result.diagnostic;
        wordResponses.push_back(std::move(payload));
    }

    auto snapshot = poller.assemble(coilResponses, wordResponses, 0);
    ASSERT_TRUE(snapshot.isTrusted());

    PlcDevice device(profile);
    device.updateSnapshot(std::move(snapshot));

    bool vBool; float vFloat; int16_t vInt16;
    EXPECT_TRUE(safeReadBool(device, plc::reg::z_axis::feedback::MOVE_DONE, vBool));
    EXPECT_TRUE(safeReadBool(device, plc::reg::z_axis::feedback::ABS_MOVING, vBool));
    EXPECT_TRUE(safeReadBool(device, plc::reg::z_axis::feedback::REL_MOVING, vBool));
    EXPECT_TRUE(safeReadBool(device, plc::reg::z_axis::feedback::JOGGING, vBool));
    EXPECT_TRUE(safeReadInt16(device, plc::reg::z_axis::feedback::STATE, vInt16));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::z_axis::feedback::ABS_POSITION, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::z_axis::feedback::REL_POSITION, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::z_axis::feedback::REL_ZERO_OFFSET, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::z_axis::feedback::REL_POSITION_OLD, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::z_axis::feedback::ABS_POSITION_OLD, vFloat));
    EXPECT_TRUE(safeReadFloat(device, plc::reg::z_axis::feedback::REL_ZERO_RECORD, vFloat));

    client->stop();
}
