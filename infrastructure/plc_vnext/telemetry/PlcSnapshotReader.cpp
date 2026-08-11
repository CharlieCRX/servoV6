// ============================================================================
// PlcSnapshotReader.cpp —— Step 7 telemetry: 读计划执行与快照组装实现
// ============================================================================
#include "infrastructure/plc_vnext/telemetry/PlcSnapshotReader.h"

#include <chrono>
#include <utility>
#include <vector>

#include "infrastructure/plc_vnext/codec/RawRegisterBlock.h"
#include "infrastructure/plc_vnext/contracts/SnapshotQuality.h"
#include "infrastructure/plc_vnext/layout/AxisSlotRegisterLayout.h"
#include "infrastructure/plc_vnext/layout/GantryLayout.h"
#include "infrastructure/plc_vnext/layout/ReadPlan.h"
#include "infrastructure/plc_vnext/layout/ReadPlanBuilder.h"
#include "infrastructure/plc_vnext/telemetry/AxisSnapshotDecoder.h"
#include "infrastructure/plc_vnext/telemetry/GantryStatusReader.h"

namespace plc_vnext::telemetry {
namespace {

// 标准 16 槽位 D 区反馈与龙门状态区，均由 layout 常量推导，保持唯一事实源：
//   - 轴区：manualSpeed(0)=D0 .. alarmWord(15)=D175 → D0..D175（176 字）。
//   - 龙门状态区：gantryStatusBase(0)=D190 .. gantryStatusBase(1)+18=D226
//     → D190..D225（36 字）。
constexpr int kAxisStart  = layout::manualSpeed(0).value();              // 0
constexpr int kAxisWords  = layout::alarmWord(15).value() + 1 - kAxisStart;  // 176
constexpr int kGantryWords = (layout::gantryStatusBase(1).value() + 18) -
                             layout::gantryStatusBase(0).value();         // 36

}  // namespace

PlcSnapshotReader::PlcSnapshotReader(transport::IModbusClientPtr client)
    : m_client(std::move(client)) {}

bool PlcSnapshotReader::readRegion(const layout::ReadPlan& plan,
                                   std::vector<uint16_t>& out, int expectedWords) {
    out.clear();
    out.reserve(static_cast<std::size_t>(expectedWords));
    for (const layout::ReadRange& range : plan) {
        if (range.area != layout::ReadArea::Holding) return false;
        std::vector<uint16_t> chunk;
        auto res = m_client->readHoldingRegisters(
            static_cast<uint16_t>(range.start), static_cast<uint16_t>(range.count), chunk);
        if (!res.ok()) return false;
        // 严格断言单次请求返回字数为请求数：底层若返回短块或超长块，寄存器会
        // 错位（后续解码把错误地址当成字段），必须使该区域整体失败而不是放行。
        if (chunk.size() != static_cast<std::size_t>(range.count)) return false;
        out.insert(out.end(), chunk.begin(), chunk.end());
    }
    return static_cast<int>(out.size()) >= expectedWords;
}

contracts::RuntimeSnapshot PlcSnapshotReader::read() {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();

    // 通过 Step 4 计划能力生成最少且合法的 FC03 读请求。
    const layout::ReadPlan axisPlan =
        layout::ReadPlanBuilder::buildHolding({{kAxisStart, kAxisWords}});
    const layout::ReadPlan gantryPlan = layout::ReadPlanBuilder::buildHolding(
        {{layout::gantryStatusBase(0).value(), kGantryWords}});

    std::vector<uint16_t> axisWords;
    const bool axisOk = readRegion(axisPlan, axisWords, kAxisWords);

    std::vector<uint16_t> gantryWords;
    const bool gantryOk = readRegion(gantryPlan, gantryWords, kGantryWords);

    contracts::RuntimeSnapshot snap;

    // 轴区：读取失败时 axisWords 为空，解码各槽位自然得到 trusted=false。
    codec::RawRegisterBlock axisBlock(kAxisStart, std::move(axisWords), 0, {}, 0);
    for (int i = 0; i < static_cast<int>(contracts::kRuntimeAxisCount); ++i) {
        snap.axes[static_cast<std::size_t>(i)] = AxisSnapshotDecoder::decode(axisBlock, i);
    }

    // 龙门区：wordStart 为 D190（gantryStatusBase(0)），getWords 用绝对地址。
    codec::RawRegisterBlock gantryBlock(layout::gantryStatusBase(0).value(),
                                        std::move(gantryWords), 0, {}, 0);
    for (int g = 0; g < static_cast<int>(contracts::kRuntimeGroupCount); ++g) {
        snap.gantry[static_cast<std::size_t>(g)] = GantryStatusReader::decode(gantryBlock, g);
    }

    // 整体质量：全部成功 Trusted；部分成功 Partial；全部失败 TransportFailed。
    if (axisOk && gantryOk) {
        snap.quality = contracts::SnapshotQuality::Trusted;
    } else if (!axisOk && !gantryOk) {
        snap.quality = contracts::SnapshotQuality::TransportFailed;
    } else {
        snap.quality = contracts::SnapshotQuality::Partial;
    }

    const auto t1 = clock::now();
    snap.sampledAtMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(t0.time_since_epoch()).count();
    snap.durationMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    return snap;
}

}  // namespace plc_vnext::telemetry
