// infrastructure/plc/protocol/PlcPoller.cpp
#include "PlcPoller.h"
#include <algorithm>
#include <stdexcept>

namespace plc::protocol {

// ============================================================================
// AddressPacker
// ============================================================================

std::vector<AddressRange> AddressPacker::pack(const std::vector<uint16_t>& addresses, uint16_t maxGap) {
  std::vector<AddressRange> ranges;
  if (addresses.empty()) return ranges;

  AddressRange current{addresses[0], 1};

  for (size_t i = 1; i < addresses.size(); ++i) {
    uint16_t gap = addresses[i] - addresses[i - 1] - 1;

    if (gap <= maxGap) {
      // 合并：current.startAddress 不变，count 需增长到覆盖本次地址
      uint16_t needed = addresses[i] - current.startAddress + 1;
      if (needed > current.count) current.count = needed;
    } else {
      // 不可合并：提交当前区间，新开一个
      ranges.push_back(current);
      current = AddressRange{addresses[i], 1};
    }
  }

  ranges.push_back(current);
  return ranges;
}


// ============================================================================
// PlcPoller
// ============================================================================

PlcPoller::PlcPoller(const RegisterRegistry& registry)
  : m_registry(registry)
{
  // 1. 提取所有 Coil 地址并排序
  auto coilRegs = m_registry.findByArea(RegisterArea::Coil);
  m_coilAddresses.clear();
  for (const auto& reg : coilRegs) {
    m_coilAddresses.push_back(reg.address);
  }
  std::sort(m_coilAddresses.begin(), m_coilAddresses.end());
  // 对于 Coil (M区)：由于 Modbus 一次最多读 2000 个位，我们可以把容忍间隙设大一点（比如 200）
  m_coilRanges = AddressPacker::pack(m_coilAddresses, 200);

  // 2. 提取所有 HoldingReg 地址并按位宽展开，再排序
  auto wordRegs = m_registry.findByArea(RegisterArea::HoldingReg);
  m_wordAddresses.clear();
  for (const auto& reg : wordRegs) {
    m_wordAddresses.push_back(reg.address);
    if (reg.type == RegisterType::Float32) {
      m_wordAddresses.push_back(reg.address + 1); // Float32 占 2 个连续字
    }
  }
  std::sort(m_wordAddresses.begin(), m_wordAddresses.end());
  // 去重（相邻寄存器可能共享同一个展开地址）
  m_wordAddresses.erase(
    std::unique(m_wordAddresses.begin(), m_wordAddresses.end()),
    m_wordAddresses.end());
  // 对于 HoldingReg (D区)：由于我们已经做了连续映射（D2000~D2055）
  // 虽然可能有个别预留的空隙，容忍个 50 左右的间隙完全足够，让它合并成一包
  m_wordRanges = AddressPacker::pack(m_wordAddresses, 50);
}

PollRequest PlcPoller::prepare() const {
  PollRequest req;

  for (const auto& range : m_coilRanges) {
    req.coilRequests.push_back(CoilReadRequest{range});
  }

  for (const auto& range : m_wordRanges) {
    req.wordRequests.push_back(WordReadRequest{range});
  }

  return req;
}

PlcSnapshot PlcPoller::assemble(
  const std::vector<std::vector<uint8_t>>&  coilResponses,
  const std::vector<std::vector<uint16_t>>& wordResponses,
  uint64_t timestamp) const
{
  bool complete = true;

  // --- 拼装 Coil 快照 ---
  RawBitSnapshot bitsSnapshot;
  if (!m_coilRanges.empty()) {
    // 检查响应数量是否匹配
    if (coilResponses.size() != m_coilRanges.size()) {
      complete = false;
    } else {
      // 计算从最小地址到最大地址 + 最后一个区间的 count 的跨度
      uint16_t firstStartAddr  = m_coilRanges[0].startAddress;
      uint16_t lastEndAddr     = m_coilRanges.back().startAddress + m_coilRanges.back().count;
      uint16_t totalBits       = lastEndAddr - firstStartAddr;
      // Coil payload: ceil(totalBits / 8)
      uint16_t byteCount = (totalBits + 7) / 8;
      std::vector<uint8_t> mergedPayload(byteCount, 0);

      // 按 ranges 顺序将每个 response 的 bits 写入 mergedPayload
      bool coilOk = true;
      for (size_t i = 0; i < m_coilRanges.size(); ++i) {
        const auto& range = m_coilRanges[i];
        const auto& resp  = coilResponses[i];
        uint16_t expectedBytes = (range.count + 7) / 8;
        if (resp.size() < expectedBytes) {
          coilOk = false;
          break;
        }
        // 本区间在 mergedPayload 中的 bit 偏移量
        size_t offset = range.startAddress - firstStartAddr;
        for (size_t byteIdx = 0; byteIdx < expectedBytes; ++byteIdx) {
          uint8_t src = resp[byteIdx];
          for (int bit = 0; bit < 8; ++bit) {
            size_t globalBit = offset + byteIdx * 8 + bit;
            if (globalBit >= totalBits) break;
            if (src & (1 << bit)) {
              mergedPayload[globalBit / 8] |= (1 << (globalBit % 8));
            }
          }
        }
      }

      if (coilOk && !m_coilRanges.empty()) {
        bitsSnapshot = RawBitSnapshot(
          firstStartAddr,
          totalBits,
          std::move(mergedPayload));
      } else {
        complete = false;
      }
    }
  }

  // --- 拼装 Word 快照 ---
  RawWordSnapshot wordsSnapshot;
  if (!m_wordRanges.empty()) {
    if (wordResponses.size() != m_wordRanges.size()) {
      complete = false;
    } else {
      // 计算从最小地址到最大地址 + 最后一个区间的 count 的跨度
      uint16_t firstStartAddr = m_wordRanges[0].startAddress;
      uint16_t lastEndAddr    = m_wordRanges.back().startAddress + m_wordRanges.back().count;
      uint16_t totalWords     = lastEndAddr - firstStartAddr;
      std::vector<uint16_t> mergedPayload(totalWords, 0);

      bool wordOk = true;
      for (size_t i = 0; i < m_wordRanges.size(); ++i) {
        const auto& range = m_wordRanges[i];
        const auto& resp  = wordResponses[i];
        if (resp.size() < range.count) {
          wordOk = false;
          break;
        }
        // 本区间在 mergedPayload 中的 word 偏移量
        size_t offset = range.startAddress - firstStartAddr;
        std::copy(resp.begin(),
                  resp.begin() + range.count,
                  mergedPayload.begin() + offset);
      }

      if (wordOk && !m_wordRanges.empty()) {
        wordsSnapshot = RawWordSnapshot(
          firstStartAddr,
          std::move(mergedPayload));
      } else {
        complete = false;
      }
    }
  }

  return PlcSnapshot(
    std::move(bitsSnapshot),
    std::move(wordsSnapshot),
    complete,
    timestamp);
}

PlcSnapshot PlcPoller::trust(
  std::vector<uint8_t>  bitsPayload,
  uint16_t        bitsStart,
  uint16_t        bitsCount,
  std::vector<uint16_t> wordsPayload,
  uint16_t        wordsStart,
  uint64_t        timestamp)
{
  RawBitSnapshot bits(bitsStart, bitsCount, std::move(bitsPayload));
  RawWordSnapshot words(wordsStart, std::move(wordsPayload));
  return PlcSnapshot(std::move(bits), std::move(words), true, timestamp);
}

PlcSnapshot PlcPoller::untrusted(uint64_t timestamp) {
  return PlcSnapshot(RawBitSnapshot{}, RawWordSnapshot{}, false, timestamp);
}

} // namespace plc::protocol
