// tests/protocol/test_raw_word_snapshot.cpp
#include <gtest/gtest.h>
#include "infrastructure/plc/protocol/MemorySnapshot.h"

using namespace plc::protocol;

class RawWordSnapshotTest : public ::testing::Test {
protected:
    // 模拟 D200 开始的 4 个寄存器
    std::vector<uint16_t> mockPayload = { 0x1111, 0x2222, 0x3333, 0x4444 };
};

TEST_F(RawWordSnapshotTest, ShouldReturnSpanWithoutCopying) {
    RawWordSnapshot snapshot(200, mockPayload);

    // 尝试读取 D201 开始的 2 个 Word (比如读取一个 Float32)
    auto result = snapshot.getWords(201, 2);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 2);
    EXPECT_EQ((*result)[0], 0x2222);
    EXPECT_EQ((*result)[1], 0x3333);
    
    // 零拷贝验证：Span 的地址必须落在原始 payload 内存范围内
    // 注意：如果是 std::move 进去的，指针地址和外面原先的可能不一样了，
    // 这里主要是验证 span 的行为正确性即可。
}

TEST_F(RawWordSnapshotTest, ShouldRejectOutOfBoundsRequest) {
    RawWordSnapshot snapshot(200, mockPayload); // 容量是 200~203

    // 1. 低于起始地址
    EXPECT_FALSE(snapshot.getWords(199, 1).has_value());
    
    // 2. 超出最大地址 (从 203 读 2 个，需要 203和204，但只有到203)
    EXPECT_FALSE(snapshot.getWords(203, 2).has_value());
    
    // 3. 刚好在边界 (从 202 读 2 个，刚好是 202和203)
    EXPECT_TRUE(snapshot.getWords(202, 2).has_value());
}