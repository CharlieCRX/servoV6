// ============================================================================
// SafetyStateReader.cpp —— 阶段 2 telemetry: 急停只读器（M224/M225）
// ============================================================================
#include "infrastructure/plc_vnext/telemetry/SafetyStateReader.h"

#include <chrono>
#include <utility>
#include <vector>

namespace plc_vnext::telemetry {

namespace {
// 设备急停线圈 0 基址（方案 §4.4：M224 急停 / M225 解除；地址原样透传不做 +1）。
constexpr uint16_t kSafetyCoilBase = 224;
// 连续读取 2 个线圈：M224..M225。
constexpr uint16_t kSafetyCoilCount = 2;
}  // namespace

SafetyStateReader::SafetyStateReader(transport::IModbusClientPtr client)
    : m_client(std::move(client)) {}

contracts::SafetySnapshot SafetyStateReader::read() {
    contracts::SafetySnapshot s;
    s.sampledAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();

    std::vector<uint8_t> bits;
    auto res = m_client->readCoils(kSafetyCoilBase, kSafetyCoilCount, bits);

    // 通讯失败或空 payload（连一个字节的位数据都没有）→ 不可信，不得冒充“正常”。
    if (!res.ok() || bits.empty()) {
        s.trusted = false;
        s.diagnostic = res.diagnostic.empty() ? "safety coil read failed"
                                              : res.diagnostic;
        return s;
    }

    s.trusted = true;
    s.emergencyStop = (bits[0] & 0x01u) != 0;  // bit0 = M224
    s.releaseRequest = (bits[0] & 0x02u) != 0; // bit1 = M225
    return s;
}

}  // namespace plc_vnext::telemetry
