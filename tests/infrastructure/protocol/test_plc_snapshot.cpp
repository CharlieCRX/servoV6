// tests/infrastructure/protocol/test_plc_snapshot.cpp
// P3: PlcSnapshot TDD 测试
// 验证"一次完整 PLC 现场照片"的一致性契约

#include <gtest/gtest.h>
#include "infrastructure/plc/protocol/PlcSnapshot.h"
#include "infrastructure/plc/protocol/MemorySnapshot.h"

using namespace plc::protocol;

// ============================================================================
// 构造与可信度
// ============================================================================

TEST(PlcSnapshotTest, Construct_CompleteSnapshot_IsTrusted) {
    RawBitSnapshot bits(101, 8, std::vector<uint8_t>{0x29});
    RawWordSnapshot words(100, std::vector<uint16_t>{0x0003, 0x0000});
    PlcSnapshot snap(std::move(bits), std::move(words), true, 1000);

    EXPECT_TRUE(snap.isTrusted());
    EXPECT_TRUE(snap.complete);
}

TEST(PlcSnapshotTest, Construct_IncompleteSnapshot_IsNotTrusted) {
    RawBitSnapshot bits;
    RawWordSnapshot words;
    PlcSnapshot snap(std::move(bits), std::move(words), false, 1000);

    EXPECT_FALSE(snap.isTrusted());
    EXPECT_FALSE(snap.complete);
}

TEST(PlcSnapshotTest, DefaultConstructor_CreatesIncompleteEmpty) {
    PlcSnapshot snap;

    EXPECT_FALSE(snap.isTrusted());
    EXPECT_FALSE(snap.complete);
    EXPECT_EQ(snap.timestamp, 0);
}

// ============================================================================
// 时间戳
// ============================================================================

TEST(PlcSnapshotTest, Timestamp_CorrectlyStored) {
    RawBitSnapshot bits;
    RawWordSnapshot words;
    PlcSnapshot snap(std::move(bits), std::move(words), true, 987654321);

    EXPECT_EQ(snap.timestamp, 987654321);
}

TEST(PlcSnapshotTest, Timestamp_ZeroAllowed) {
    RawBitSnapshot bits;
    RawWordSnapshot words;
    PlcSnapshot snap(std::move(bits), std::move(words), true, 0);

    EXPECT_EQ(snap.timestamp, 0);
    EXPECT_TRUE(snap.isTrusted());
}

// ============================================================================
// 子快照可访问性
// ============================================================================

TEST(PlcSnapshotTest, BitSnapshot_Accessible) {
    std::vector<uint8_t> payload{0x29}; // 0b00101001
    RawBitSnapshot bits(101, 8, payload);
    RawWordSnapshot words;
    PlcSnapshot snap(std::move(bits), std::move(words), true, 1000);

    auto val = snap.bits.getBit(101);
    ASSERT_TRUE(val.has_value());
    EXPECT_TRUE(val.value());
}

TEST(PlcSnapshotTest, WordSnapshot_Accessible) {
    RawBitSnapshot bits;
    RawWordSnapshot words(100, std::vector<uint16_t>{0x0003, 0x0000});
    PlcSnapshot snap(std::move(bits), std::move(words), true, 1000);

    auto span = snap.words.getWords(100, 1);
    ASSERT_TRUE(span.has_value());
    EXPECT_EQ((*span)[0], 3);
}

TEST(PlcSnapshotTest, BothSnapshots_AccessibleAfterMove) {
    std::vector<uint8_t> coilPayload{0x01};
    std::vector<uint16_t> wordPayload{42, 0};
    RawBitSnapshot bits(1, 1, coilPayload);
    RawWordSnapshot words(1, wordPayload);
    PlcSnapshot snap(std::move(bits), std::move(words), true, 500);

    // Both should be accessible
    auto bit = snap.bits.getBit(1);
    ASSERT_TRUE(bit.has_value());
    EXPECT_TRUE(bit.value());

    auto span = snap.words.getWords(1, 1);
    ASSERT_TRUE(span.has_value());
    EXPECT_EQ((*span)[0], 42);
}

// ============================================================================
// move 语义
// ============================================================================

TEST(PlcSnapshotTest, MoveConstructor_PreservesData) {
    std::vector<uint8_t> coilPayload{0x02}; // bit1=true
    RawBitSnapshot bits(100, 8, coilPayload);
    RawWordSnapshot words(50, std::vector<uint16_t>{7, 8, 9});
    PlcSnapshot original(std::move(bits), std::move(words), true, 3000);

    PlcSnapshot moved(std::move(original));

    EXPECT_TRUE(moved.isTrusted());
    EXPECT_EQ(moved.timestamp, 3000);

    auto bit = moved.bits.getBit(101); // offset=1 from base 100
    ASSERT_TRUE(bit.has_value());
    EXPECT_TRUE(bit.value());

    auto span = moved.words.getWords(51, 1);
    ASSERT_TRUE(span.has_value());
    EXPECT_EQ((*span)[0], 8);
}

TEST(PlcSnapshotTest, MoveAssignment_PreservesData) {
    std::vector<uint8_t> coilPayload{0x01};
    RawBitSnapshot bits(200, 1, coilPayload);
    RawWordSnapshot words(300, std::vector<uint16_t>{99});
    PlcSnapshot source(std::move(bits), std::move(words), false, 999);

    PlcSnapshot target;
    target = std::move(source);

    EXPECT_FALSE(target.isTrusted());
    EXPECT_EQ(target.timestamp, 999);

    auto bit = target.bits.getBit(200);
    ASSERT_TRUE(bit.has_value());
    EXPECT_TRUE(bit.value());

    auto span = target.words.getWords(300, 1);
    ASSERT_TRUE(span.has_value());
    EXPECT_EQ((*span)[0], 99);
}

// ============================================================================
// 边界场景
// ============================================================================

TEST(PlcSnapshotTest, EmptySnapshots_WithCompleteTrue_IsTrustedButEmpty) {
    RawBitSnapshot bits;
    RawWordSnapshot words;
    PlcSnapshot snap(std::move(bits), std::move(words), true, 100);

    EXPECT_TRUE(snap.isTrusted());

    // Empty snapshots return nullopt for any access
    EXPECT_FALSE(snap.bits.getBit(0).has_value());
    EXPECT_FALSE(snap.words.getWords(0, 1).has_value());
}

TEST(PlcSnapshotTest, PartialSnapshot_CoilOnly_CompleteTrue) {
    // 场景：只读了一次 FC01，未读 FC03
    std::vector<uint8_t> coilPayload{0xFF};
    RawBitSnapshot bits(0, 8, coilPayload);
    RawWordSnapshot words; // empty
    PlcSnapshot snap(std::move(bits), std::move(words), true, 100);

    EXPECT_TRUE(snap.isTrusted());
    EXPECT_TRUE(snap.bits.getBit(0).value());
    EXPECT_FALSE(snap.words.getWords(0, 1).has_value());
}

TEST(PlcSnapshotTest, PartialSnapshot_WordOnly_CompleteTrue) {
    // 场景：只读了一次 FC03，未读 FC01
    RawBitSnapshot bits;
    RawWordSnapshot words(0, std::vector<uint16_t>{100, 200});
    PlcSnapshot snap(std::move(bits), std::move(words), true, 100);

    EXPECT_TRUE(snap.isTrusted());
    EXPECT_FALSE(snap.bits.getBit(0).has_value());
    auto span = snap.words.getWords(0, 2);
    ASSERT_TRUE(span.has_value());
    EXPECT_EQ((*span)[0], 100);
    EXPECT_EQ((*span)[1], 200);
}
