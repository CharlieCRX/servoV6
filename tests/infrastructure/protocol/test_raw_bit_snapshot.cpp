// tests/protocol/test_raw_bit_snapshot.cpp
#include <gtest/gtest.h>
#include "infrastructure/plc/protocol/MemorySnapshot.h"

using namespace plc::protocol;

class RawBitSnapshotTest : public ::testing::Test {
protected:
    // 模拟从 Modbus 收到的 2 个字节数据
    // Byte 0: 0b00000101 (十进制 5) -> Bit0=1, Bit1=0, Bit2=1
    // Byte 1: 0b10000000 (十进制 128) -> Bit7=1, 其他为0
    std::vector<uint8_t> mockPayload = { 0x05, 0x80 };
};

TEST_F(RawBitSnapshotTest, ShouldReadBitsCorrectlyInFirstByte) {
    // 假设抓取的是 M100 开始的 16 个位
    RawBitSnapshot snapshot(100, 16, mockPayload);

    // Act & Assert
    EXPECT_TRUE(snapshot.getBit(100).value());  // 100 对应 Byte0 的 Bit0 (1)
    EXPECT_FALSE(snapshot.getBit(101).value()); // 101 对应 Byte0 的 Bit1 (0)
    EXPECT_TRUE(snapshot.getBit(102).value());  // 102 对应 Byte0 的 Bit2 (1)
}

TEST_F(RawBitSnapshotTest, ShouldReadBitsCorrectlyInSecondByte) {
    RawBitSnapshot snapshot(100, 16, mockPayload);

    // M108 开始进入 Byte 1
    EXPECT_FALSE(snapshot.getBit(108).value()); // 108 对应 Byte1 的 Bit0 (0)
    EXPECT_TRUE(snapshot.getBit(115).value());  // 115 对应 Byte1 的 Bit7 (1)
}

TEST_F(RawBitSnapshotTest, ShouldReturnNulloptWhenAddressIsOutOfBounds) {
    RawBitSnapshot snapshot(100, 16, mockPayload); // 只包含 100 ~ 115

    EXPECT_FALSE(snapshot.getBit(99).has_value());  // 低于起始地址
    EXPECT_FALSE(snapshot.getBit(116).has_value()); // 高于/等于最大地址
}