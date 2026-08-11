// ============================================================================
// RawRegisterBlock.cpp —— Step 2 codec: 原始寄存器缓冲实现
// ============================================================================
#include "infrastructure/plc_vnext/codec/RawRegisterBlock.h"

#include <cstddef>
#include <stdexcept>
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
      bitCount_(bitCount) {
    if (bitCount_ < 0) {
        throw std::invalid_argument(
            "RawRegisterBlock: bitCount must be >= 0");
    }
    // 用宽整数计算位缓冲容量，避免 bits.size()*8 溢出
    const long long capacity =
        static_cast<long long>(bits_.size()) * 8LL;
    if (static_cast<long long>(bitCount_) > capacity) {
        throw std::invalid_argument(
            "RawRegisterBlock: bitCount exceeds bits buffer capacity");
    }
}

std::optional<std::span<const uint16_t>> RawRegisterBlock::getWords(
    int address, int count) const {
    if (count < 0) return std::nullopt;
    // 全程用 64 位中间量，避免 wordStart_ + words_.size() 在近整数边界时溢出
    const long long a = address;
    const long long start = wordStart_;
    const long long upper =
        static_cast<long long>(wordStart_) + words_.size();
    if (a < start || a > upper) return std::nullopt;
    const long long end = a + count;
    if (end > upper) return std::nullopt;

    const std::size_t offset = static_cast<std::size_t>(a - start);
    return std::span<const uint16_t>(words_.data() + offset,
                                     static_cast<std::size_t>(count));
}

std::optional<bool> RawRegisterBlock::getBit(int address) const {
    // 用 64 位中间量计算边界，避免 bitStart_ + bitCount_ 溢出
    const long long a = address;
    const long long low = bitStart_;
    const long long high = static_cast<long long>(bitStart_) + bitCount_;
    if (a < low || a >= high) return std::nullopt;

    const long long offset = a - low;
    const std::size_t byteIndex =
        static_cast<std::size_t>(offset / 8);
    const unsigned bitIndex = static_cast<unsigned>(offset % 8);
    return (bits_[byteIndex] & (1u << bitIndex)) != 0u;
}

}  // namespace plc_vnext::codec
