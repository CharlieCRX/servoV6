// ============================================================================
// test_snapshot_reader.cpp —— Step 7 telemetry: PlcSnapshotReader
// ============================================================================
// 红：引用 infrastructure/plc_vnext/telemetry/PlcSnapshotReader.h；对应 7.1
// 红用例（用 FakeModbus）：
//   ExecutesPlan_ProduceRuntimeSnapshot / PartialReadFailure_QualityPartial /
//   TransportFailure_QualityFailed / SnapshotHasSampledAt_AndDuration
// 轴区 D0..D175，龙门状态区 D190..D225。
// ============================================================================
#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/fake/FakeModbusClient.h"
#include "infrastructure/plc_vnext/layout/AxisSlotRegisterLayout.h"
#include "infrastructure/plc_vnext/layout/GantryLayout.h"
#include "infrastructure/plc_vnext/contracts/SnapshotQuality.h"
#include "infrastructure/plc_vnext/telemetry/PlcSnapshotReader.h"
#include "tests/infrastructure/plc_vnext/support/TelemetryFixture.h"

namespace plc_vnext::telemetry {
namespace {

using contracts::SnapshotQuality;

/// 可编程返回块长度的 IModbusClient 替身：用于模拟底层返回短块 / 超长块。
/// 对轴区第二个分片（start==125）返回 (count+delta_) 个字，其余返回正确长度。
class ChunkSizeStub : public transport::IModbusClient {
public:
    explicit ChunkSizeStub(int axisChunkDelta) : axisChunkDelta_(axisChunkDelta) {}

    contracts::CommunicationResult readHoldingRegisters(
        uint16_t start, uint16_t count, std::vector<uint16_t>& payload) override {
        if (start == 125) {  // 轴区第二个 125 分片（D125..D175，count=51）
            payload.assign(static_cast<std::size_t>(count) + axisChunkDelta_, 0);
        } else {
            payload.assign(count, 0);
        }
        return contracts::CommunicationResult::sent();
    }

    bool isConnected() const override { return true; }
    void requestReconnect() override {}
    contracts::CommunicationResult readCoils(uint16_t, uint16_t,
                                             std::vector<uint8_t>& p) override {
        p.clear();
        return contracts::CommunicationResult::sent();
    }
    contracts::CommunicationResult writeSingleCoil(uint16_t, bool) override {
        return contracts::CommunicationResult::sent();
    }
    contracts::CommunicationResult writeSingleRegister(uint16_t, uint16_t) override {
        return contracts::CommunicationResult::sent();
    }
    contracts::CommunicationResult writeMultipleRegisters(
        uint16_t, const std::vector<uint16_t>&) override {
        return contracts::CommunicationResult::sent();
    }

private:
    int axisChunkDelta_;
};

// 龙门状态块向量下标 0 == D190（gantryStatusBase(0)）。
inline int gantryIndexOffset(int group, int offset) {
    return layout::gantryStatusBase(group).value() -
           layout::gantryStatusBase(0).value() + offset;
}

// 轴区块按绝对 D 地址写入 Fake RAM（基址 0）。
void loadAxis(fake::FakeModbusClient& fake, const std::vector<uint16_t>& block) {
    for (std::size_t i = 0; i < block.size(); ++i) {
        fake.setHoldingRegister(static_cast<uint16_t>(i), block[i]);
    }
}

// 龙门状态区块（下标 0 == D190）写入 Fake RAM。
void loadGantry(fake::FakeModbusClient& fake, const std::vector<uint16_t>& block) {
    const uint16_t base = static_cast<uint16_t>(layout::gantryStatusBase(0).value());
    for (std::size_t i = 0; i < block.size(); ++i) {
        fake.setHoldingRegister(static_cast<uint16_t>(base + i), block[i]);
    }
}

// 参数区（下标 0 == D1064，覆盖 D1064..D1243）写入 Fake RAM。
void loadParam(fake::FakeModbusClient& fake, const std::vector<uint16_t>& block) {
    const uint16_t base = static_cast<uint16_t>(layout::relZeroRecord(0).value());
    for (std::size_t i = 0; i < block.size(); ++i) {
        fake.setHoldingRegister(static_cast<uint16_t>(base + i), block[i]);
    }
}

TEST(SnapshotReaderTest, ExecutesPlan_ProduceRuntimeSnapshot) {
    auto fake = std::make_shared<fake::FakeModbusClient>();

    auto axis = test::makeAxisBlock();
    test::writeFloat(axis, layout::absPosition(0).value(), 100.0f);
    test::writeFloat(axis, layout::absPosition(15).value(), -3.5f);
    loadAxis(*fake, axis);

    auto gantry = test::makeGantryBlock();
    test::writeInt16(gantry, gantryIndexOffset(0, 0), 3);  // group0 State=已联动
    loadGantry(*fake, gantry);

    PlcSnapshotReader reader(fake);
    auto snap = reader.read();

    EXPECT_EQ(snap.quality, SnapshotQuality::Trusted);
    ASSERT_EQ(snap.axes.size(), 16u);
    for (const auto& a : snap.axes) {
        EXPECT_TRUE(a.trusted);
    }
    EXPECT_FLOAT_EQ(snap.axes[0].absPosition, 100.0f);
    EXPECT_FLOAT_EQ(snap.axes[15].absPosition, -3.5f);
    EXPECT_TRUE(snap.gantry[0].trusted);
    EXPECT_EQ(snap.gantry[0].state, 3);
}

TEST(SnapshotReaderTest, PartialReadFailure_QualityPartial) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    loadAxis(*fake, test::makeAxisBlock());

    // 只让龙门区（D190 起）失败：轴区成功、龙门区失败 → Partial
    fake->setFailureThreshold(static_cast<uint16_t>(layout::gantryStatusBase(0).value()));

    PlcSnapshotReader reader(fake);
    auto snap = reader.read();

    EXPECT_EQ(snap.quality, SnapshotQuality::Partial);
    for (const auto& a : snap.axes) {
        EXPECT_TRUE(a.trusted);
    }
    for (const auto& g : snap.gantry) {
        EXPECT_FALSE(g.trusted);
    }
}

TEST(SnapshotReaderTest, TransportFailure_QualityFailed) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    fake->setFailureThreshold(0);  // 全部读请求失败

    PlcSnapshotReader reader(fake);
    auto snap = reader.read();

    EXPECT_EQ(snap.quality, SnapshotQuality::TransportFailed);
    for (const auto& a : snap.axes) {
        EXPECT_FALSE(a.trusted);
    }
    for (const auto& g : snap.gantry) {
        EXPECT_FALSE(g.trusted);
    }
}

TEST(SnapshotReaderTest, SnapshotHasSampledAt_AndDuration) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    loadAxis(*fake, test::makeAxisBlock());
    loadGantry(*fake, test::makeGantryBlock());

    PlcSnapshotReader reader(fake);
    auto snap = reader.read();

    EXPECT_GT(snap.sampledAtMs, 0);
    EXPECT_GE(snap.durationMs, 0);
}

TEST(SnapshotReaderTest, AxisShortChunk_MarksRegionUntrusted_Partial) {
    // 轴区第二个 125 分片返回短块（count-1=50）→ 该区域整体 trusted=false，
    // 龙门区仍成功 → 整体质量 Partial（不得把错位数据放行进解码）。
    auto client = std::make_shared<ChunkSizeStub>(-1);

    PlcSnapshotReader reader(client);
    auto snap = reader.read();

    EXPECT_EQ(snap.quality, SnapshotQuality::Partial);
    for (const auto& a : snap.axes) {
        EXPECT_FALSE(a.trusted);
    }
    for (const auto& g : snap.gantry) {
        EXPECT_TRUE(g.trusted);
    }
}

TEST(SnapshotReaderTest, AxisOverlongChunk_MarksRegionUntrusted_Partial) {
    // 轴区第二个 125 分片返回超长块（count+1=52）→ 同样使该区域整体失败。
    auto client = std::make_shared<ChunkSizeStub>(+1);

    PlcSnapshotReader reader(client);
    auto snap = reader.read();

    EXPECT_EQ(snap.quality, SnapshotQuality::Partial);
    for (const auto& a : snap.axes) {
        EXPECT_FALSE(a.trusted);
    }
    for (const auto& g : snap.gantry) {
        EXPECT_TRUE(g.trusted);
    }
}

TEST(SnapshotReaderTest, DecodesSoftLimitParamsFromParamRegion) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    loadAxis(*fake, test::makeAxisBlock());
    loadGantry(*fake, test::makeGantryBlock());

    // 参数区 D1064..D1243：预置槽位 0 软负/软正/控制字。
    const int base = layout::relZeroRecord(0).value();
    auto param = std::vector<uint16_t>(180, 0);
    test::writeFloat(param, layout::softNegLimit(0).value() - base, -500.0f);
    test::writeFloat(param, layout::softPosLimit(0).value() - base, 500.0f);
    test::writeWord(param, layout::softLimitControl(0).value() - base, 0x0003);
    loadParam(*fake, param);

    PlcSnapshotReader reader(fake);
    auto snap = reader.read();

    // 整体 quality 只看轴/龙门区，不受参数区影响。
    EXPECT_EQ(snap.quality, SnapshotQuality::Trusted);
    EXPECT_TRUE(snap.params[0].trusted);
    EXPECT_FLOAT_EQ(snap.params[0].softNegLimit, -500.0f);
    EXPECT_FLOAT_EQ(snap.params[0].softPosLimit, 500.0f);
    EXPECT_EQ(snap.params[0].softLimitControl, 0x0003u);
    // 未预置的槽位 1：参数区整块读出（全 0），trusted 仍为 true。
    EXPECT_TRUE(snap.params[1].trusted);
}

}  // namespace
}  // namespace plc_vnext::telemetry
