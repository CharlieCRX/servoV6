// ============================================================================
// test_raw_register_block.cpp —— Step 2 codec: 原始寄存器缓冲
// ============================================================================
// 红：本文件引用的 codec/RawRegisterBlock.h 尚不存在，预期编译失败；实现后全绿。
// 验证 RawRegisterBlock 只按地址安全读取原始 word/bit，越界返回 nullopt 不抛异常，
// 不承载任何业务语义。
// ============================================================================
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/codec/RawRegisterBlock.h"

namespace plc_vnext::codec {
namespace {

TEST(RawRegisterBlockTest, GetWords_ReadsRawValuesAtOffset) {
    // wordStart=10，覆盖地址 10..12
    RawRegisterBlock block(10, std::vector<uint16_t>{0x1111, 0x2222, 0x3333},
                           /*bitStart=*/0, /*bits=*/{}, /*bitCount=*/0);
    auto span = block.getWords(11, 2);
    ASSERT_TRUE(span.has_value());
    ASSERT_EQ(span->size(), 2u);
    EXPECT_EQ((*span)[0], 0x2222);
    EXPECT_EQ((*span)[1], 0x3333);
}

TEST(RawRegisterBlockTest, GetWords_OutOfRange_ReturnsNullopt) {
    RawRegisterBlock block(10, std::vector<uint16_t>{0x1111, 0x2222, 0x3333},
                           0, {}, 0);
    // 起点在缓冲内但长度越界
    EXPECT_FALSE(block.getWords(11, 3).has_value());
    // 起点越界
    EXPECT_FALSE(block.getWords(13, 1).has_value());
    // 起点在起始地址之前
    EXPECT_FALSE(block.getWords(9, 1).has_value());
    // 负长度
    EXPECT_FALSE(block.getWords(10, -1).has_value());
}

TEST(RawRegisterBlockTest, GetBit_ReadsIndividualBit) {
    // bitStart=100，覆盖 100..115（2 字节）。字节0=0b00000011 -> bit0/bit1 为 true
    RawRegisterBlock block(0, {}, 100, std::vector<uint8_t>{0x03, 0x00}, 16);
    EXPECT_TRUE(block.getBit(100).has_value() && *block.getBit(100));
    EXPECT_TRUE(block.getBit(101).has_value() && *block.getBit(101));
    EXPECT_TRUE(block.getBit(102).has_value() && !*block.getBit(102));
    EXPECT_TRUE(block.getBit(108).has_value() && !*block.getBit(108));  // 字节1 bit0
}

TEST(RawRegisterBlockTest, GetBit_OutOfRange_ReturnsNullopt) {
    RawRegisterBlock block(0, {}, 100, std::vector<uint8_t>{0x03, 0x00}, 16);
    EXPECT_FALSE(block.getBit(99).has_value());
    EXPECT_FALSE(block.getBit(116).has_value());
}

TEST(RawRegisterBlockTest, Accessors_ExposeBounds) {
    RawRegisterBlock block(10, std::vector<uint16_t>{0x1111, 0x2222},
                           100, std::vector<uint8_t>{0x03, 0x00}, 16);
    EXPECT_EQ(block.wordStart(), 10);
    EXPECT_EQ(block.wordCount(), 2);
    EXPECT_EQ(block.bitStart(), 100);
    EXPECT_EQ(block.bitCount(), 16);
}

TEST(RawRegisterBlockTest, Constructor_RejectsNegativeBitCount) {
    EXPECT_THROW(
        RawRegisterBlock(0, {}, 0, std::vector<uint8_t>{0x00}, -1),
        std::invalid_argument);
}

TEST(RawRegisterBlockTest, Constructor_RejectsBitCountExceedingBuffer) {
    // bitCount=16 但只提供 1 字节（容量 8 位）：越界，必须拒绝
    EXPECT_THROW(
        RawRegisterBlock(0, {}, 0, std::vector<uint8_t>{0x03}, 16),
        std::invalid_argument);
    // 空缓冲却要求 1 位同样非法
    EXPECT_THROW(RawRegisterBlock(0, {}, 0, {}, 1), std::invalid_argument);
}

TEST(RawRegisterBlockTest, Constructor_AcceptsExactBoundaryCapacity) {
    // bitCount == bits.size()*8：恰好容纳，合法
    RawRegisterBlock block(0, {}, 0, std::vector<uint8_t>{0x03, 0x00}, 16);
    EXPECT_EQ(block.bitCount(), 16);
}

TEST(RawRegisterBlockTest, GetBit_BoundaryWithinCapacity) {
    // bitStart=100，bitCount=8（1 字节）：最高有效位为 offset 7
    RawRegisterBlock block(0, {}, 100, std::vector<uint8_t>{0x00, 0x00}, 8);
    // offset 7 在界内
    EXPECT_TRUE(block.getBit(100 + 7).has_value());
    // offset 8 越界（bitCount=8 只覆盖 100..107）
    EXPECT_FALSE(block.getBit(100 + 8).has_value());
}

TEST(RawRegisterBlockTest, GetWords_NearIntMax_NoOverflow) {
    // wordStart 接近 INT_MAX：旧实现 wordStart_ + words_.size() 用 int 会溢出，
    // 新实现用宽整数计算仍应正确返回 / 判定越界，且测试自身不产生整数溢出。
    const int kStart = std::numeric_limits<int>::max() - 1;
    RawRegisterBlock block(kStart, std::vector<uint16_t>{0x1111, 0x2222},
                           0, {}, 0);
    // 合法：地址 kStart..kStart+1，upper = INT_MAX+1（宽整数，不溢出）
    auto span = block.getWords(kStart, 2);
    ASSERT_TRUE(span.has_value());
    EXPECT_EQ((*span)[0], 0x1111);
    EXPECT_EQ((*span)[1], 0x2222);
    // 长度越界仍返回 nullopt，且不发生整数溢出
    EXPECT_FALSE(block.getWords(kStart + 1, 2).has_value());
    // 起点在起始地址之前
    EXPECT_FALSE(block.getWords(kStart - 1, 1).has_value());
}

TEST(RawRegisterBlockTest, GetBit_NearIntMax_NoOverflow) {
    // bitStart 接近 INT_MAX：旧实现 bitStart_ + bitCount_ 用 int 会溢出（wrap 为负），
    // 使界内位被误判为越界；新实现用宽整数计算边界，界内位应正常返回。
    // bitCount=2 只覆盖 kStart 与 kStart+1 两个地址，二者都可用 int 表示。
    const int kStart = std::numeric_limits<int>::max() - 1;
    RawRegisterBlock block(0, {}, kStart, std::vector<uint8_t>{0x03}, 2);
    EXPECT_TRUE(block.getBit(kStart).has_value());
    EXPECT_TRUE(block.getBit(kStart + 1).has_value());
    EXPECT_FALSE(block.getBit(kStart - 1).has_value());
}

}  // namespace
}  // namespace plc_vnext::codec
