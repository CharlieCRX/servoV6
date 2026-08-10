// ============================================================================
// RawRegisterBlock.cpp —— Step 2 codec: 原始寄存器缓冲实现
// ============================================================================
#include "infrastructure/plc_vnext/codec/RawRegisterBlock.h"

#include <cstddef>
#include <utility>

namespace plc_vnext::codec {

RawRegisterBlock::RawRegisterBlock(int wordStart,
                                   std::vector<uint16_t> words,
                                   int bitStart,
                                   std::vector<uint8_t> bits,
                                   int bitCount)
    : wordStart_(wordStart),
      words_(std::move(words)),
      bitStart_(bitStart),
      bits_(std::move(bits)),
      bitCount_(bitCount) {}

std::optional<std::span<const uint16_t>> RawRegisterBlock::getWords(
    int address, int count) const {
    if (count < 0) return std::nullopt;
    if (address < wordStart_ || address > wordStart_ + static_cast<int>(words_.size())) {
        return std::nullopt;
    }
    // 防溢出：用 64 位中间量判断 address + count 是否越界
    const long long end = static_cast<long long>(address) + count;
    const long long upper =
        static_cast<long long>(wordStart_) + words_.size();
    if (end > upper) return std::nullopt;

    const std::size_t offset =
        static_cast<std::size_t>(address - wordStart_);
    return std::span<const uint16_t>(words_.data() + offset,
                                     static_cast<std::size_t>(count));
}

std::optional<bool> RawRegisterBlock::getBit(int address) const {
    if (address < bitStart_ || address >= bitStart_ + bitCount_) {
        return std::nullopt;
    }
    const int offset = address - bitStart_;
    const std::size_t byteIndex = static_cast<std::size_t>(offset) / 8u;
    const unsigned bitIndex = static_cast<unsigned>(offset) % 8u;
    return (bits_[byteIndex] & (1u << bitIndex)) != 0u;
}

}  // namespace plc_vnext::codec
