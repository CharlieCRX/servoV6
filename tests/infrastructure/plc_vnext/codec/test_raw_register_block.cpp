// ============================================================================
// test_raw_register_block.cpp —— Step 2 codec: 原始寄存器缓冲
// ============================================================================
// 红：本文件引用的 codec/RawRegisterBlock.h 尚不存在，预期编译失败；实现后全绿。
// 验证 RawRegisterBlock 只按地址安全读取原始 word/bit，越界返回 nullopt 不抛异常，
// 不承载任何业务语义。
// ============================================================================
#include <cstdint>
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

}  // namespace
}  // namespace plc_vnext::codec
