// infrastructure/plc/protocol/MemorySnapshot.h
#pragma once
#include <vector>
#include <cstdint>
#include <optional>
#include <span>

namespace plc::protocol {

class RawBitSnapshot {
private:
  uint16_t m_startAddress = 0;
  uint16_t m_bitCount = 0;
  std::vector<uint8_t> m_payload;

public:
  RawBitSnapshot() = default;
  RawBitSnapshot(uint16_t startAddress, uint16_t bitCount, std::vector<uint8_t> payload)
    : m_startAddress(startAddress), m_bitCount(bitCount), m_payload(std::move(payload)) {}

  std::optional<bool> getBit(uint16_t address) const {
    if (address < m_startAddress || address >= m_startAddress + m_bitCount) {
      return std::nullopt;
    }
    
    uint16_t offset = address - m_startAddress;
    uint16_t byteIndex = offset / 8;
    uint16_t bitIndex = offset % 8;
    
    return (m_payload[byteIndex] & (1 << bitIndex)) != 0;
  }
};

class RawWordSnapshot {
private:
  uint16_t m_startAddress = 0;
  std::vector<uint16_t> m_payload;

public:
  RawWordSnapshot() = default;
  RawWordSnapshot(uint16_t startAddress, std::vector<uint16_t> payload)
    : m_startAddress(startAddress), m_payload(std::move(payload)) {}

  std::optional<std::span<const uint16_t>> getWords(uint16_t address, uint16_t count) const {

    if (address < m_startAddress || address + count > m_startAddress + m_payload.size()) {
      return std::nullopt; 
    }

    uint16_t offset = address - m_startAddress;
    
    return std::span<const uint16_t>(m_payload.data() + offset, count);
  }
};

} // namespace plc::protocol
