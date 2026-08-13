// ============================================================================
// test_safety_state_reader.cpp —— 阶段 2：急停只读器 SafetyStateReader
// ============================================================================
// 依据《servoV6剩余迁移工作实施方案》§4.4 / §10.2：
//   - M224（设备急停）/ M225（解除请求）由独立安全状态读取器经同一个串行
//     I/O 通道（IModbusClient）只读，纳入阶段 2“真实只读影子运行”。
//   - 读取失败不以“正常”冒充：trusted=false + diagnostic 保留。
// 全部经 FakeModbusClient 离线执行，不接触真实 PLC / AsioModbusTcpClient。
// 红：引用 infrastructure/plc_vnext/telemetry/SafetyStateReader.h（尚不存在）。
// ============================================================================
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/contracts/SafetySnapshot.h"
#include "infrastructure/plc_vnext/fake/FakeModbusClient.h"
#include "infrastructure/plc_vnext/telemetry/SafetyStateReader.h"

namespace plc_vnext::telemetry {
namespace {

using contracts::CommunicationResult;
using contracts::SafetySnapshot;

// 模拟“通讯 ok 但读回空 payload”——不允许把该情况标为可信。
class EmptyPayloadClient : public fake::FakeModbusClient {
public:
    contracts::CommunicationResult readCoils(uint16_t, uint16_t,
                                             std::vector<uint8_t>& payload) override {
        payload.clear();
        return contracts::CommunicationResult::sent();
    }
};

TEST(SafetyStateReaderTest, ReadsM224AndM225Bits) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    fake->setCoil(224, true);   // M224 设备急停
    fake->setCoil(225, true);   // M225 解除请求

    SafetyStateReader reader(fake);
    SafetySnapshot s = reader.read();

    EXPECT_TRUE(s.trusted);
    EXPECT_TRUE(s.emergencyStop);
    EXPECT_TRUE(s.releaseRequest);
}

TEST(SafetyStateReaderTest, OnlyM224Set) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    fake->setCoil(224, true);
    fake->setCoil(225, false);

    SafetyStateReader reader(fake);
    SafetySnapshot s = reader.read();

    ASSERT_TRUE(s.trusted);
    EXPECT_TRUE(s.emergencyStop);
    EXPECT_FALSE(s.releaseRequest);
}

TEST(SafetyStateReaderTest, OnlyM225Set) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    fake->setCoil(224, false);
    fake->setCoil(225, true);

    SafetyStateReader reader(fake);
    SafetySnapshot s = reader.read();

    ASSERT_TRUE(s.trusted);
    EXPECT_FALSE(s.emergencyStop);
    EXPECT_TRUE(s.releaseRequest);
}

TEST(SafetyStateReaderTest, AllCoilsOff_StillTrusted) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    fake->setCoil(224, false);
    fake->setCoil(225, false);

    SafetyStateReader reader(fake);
    SafetySnapshot s = reader.read();

    EXPECT_TRUE(s.trusted);
    EXPECT_FALSE(s.emergencyStop);
    EXPECT_FALSE(s.releaseRequest);
}

TEST(SafetyStateReaderTest, TransportFailure_NotTrusted_KeepsDiagnostic) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    fake->scriptTransportFailure(CommunicationResult::Status::Timeout,
                                 "safety coil read timed out");

    SafetyStateReader reader(fake);
    SafetySnapshot s = reader.read();

    EXPECT_FALSE(s.trusted);
    EXPECT_FALSE(s.emergencyStop);
    EXPECT_FALSE(s.releaseRequest);
    EXPECT_FALSE(s.diagnostic.empty());
}

// 空 payload（读回 ok 但无位数据）不得标记为可信 —— 不得以“正常”冒充。
TEST(SafetyStateReaderTest, EmptyPayload_NotTrusted) {
    auto fake = std::make_shared<EmptyPayloadClient>();

    SafetyStateReader reader(fake);
    SafetySnapshot s = reader.read();

    EXPECT_FALSE(s.trusted);
    EXPECT_FALSE(s.emergencyStop);
    EXPECT_FALSE(s.releaseRequest);
}

}  // namespace
}  // namespace plc_vnext::telemetry
