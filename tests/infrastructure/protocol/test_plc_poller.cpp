// tests/infrastructure/protocol/test_plc_poller.cpp
// P3: PlcPoller + AddressPacker TDD 测试
// 三级测试结构：
//   Level 1: AddressPacker 地址打包
//   Level 2: PlcPoller.prepare() 请求生成
//   Level 3: PlcPoller.assemble() 快照拼装及相关

#include <gtest/gtest.h>
#include "infrastructure/plc/protocol/PlcPoller.h"
#include "infrastructure/plc/protocol/RegisterRegistry.h"
#include "infrastructure/plc/protocol/RegisterMetadata.h"

using namespace plc::protocol;

// ============================================================================
// 测试辅助：构建 Registry
// ============================================================================

// 4 个 Coil: 100, 101, 102, 200
// 4 个 HoldingReg: D100(Int16), D101(Float32), D103(Int16), D200(Int16)
static RegisterRegistry makeTestRegistry() {
    RegisterRegistry reg;
    reg.addAll({
        {RegisterArea::Coil,       100, RegisterType::Bool,    RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "C100", 0, std::nullopt},
        {RegisterArea::Coil,       101, RegisterType::Bool,    RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "C101", 0, std::nullopt},
        {RegisterArea::Coil,       102, RegisterType::Bool,    RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "C102", 0, std::nullopt},
        {RegisterArea::Coil,       200, RegisterType::Bool,    RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "C200", 0, std::nullopt},

        {RegisterArea::HoldingReg, 100, RegisterType::Int16,   RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "D100 Int16", 0, std::nullopt},
        {RegisterArea::HoldingReg, 101, RegisterType::Float32, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "D101 Float32", 0, std::nullopt},
        {RegisterArea::HoldingReg, 103, RegisterType::Int16,   RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "D103 Int16", 0, std::nullopt},
        {RegisterArea::HoldingReg, 200, RegisterType::Int16,   RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "D200 Int16", 0, std::nullopt},
    });
    return reg;
}


// ============================================================================
// Level 1: AddressPacker 地址打包测试
// ============================================================================

TEST(AddressPackerTest, Pack_Empty_ReturnsEmpty) {
    auto ranges = AddressPacker::pack({}, 0);
    EXPECT_TRUE(ranges.empty());
}

TEST(AddressPackerTest, Pack_SingleAddress_ReturnsOneRange) {
    auto ranges = AddressPacker::pack({42}, 0);
    ASSERT_EQ(ranges.size(), 1);
    EXPECT_EQ(ranges[0].startAddress, 42);
    EXPECT_EQ(ranges[0].count, 1);
}

TEST(AddressPackerTest, Pack_Continuous_ZeroGap_MergedIntoOne) {
    auto ranges = AddressPacker::pack({100, 101, 102}, 0);
    ASSERT_EQ(ranges.size(), 1);
    EXPECT_EQ(ranges[0].startAddress, 100);
    EXPECT_EQ(ranges[0].count, 3);
}

TEST(AddressPackerTest, Pack_WithGap_ZeroMaxGap_SplitIntoTwo) {
    auto ranges = AddressPacker::pack({100, 101, 102, 200}, 0);
    ASSERT_EQ(ranges.size(), 2);
    EXPECT_EQ(ranges[0].startAddress, 100);
    EXPECT_EQ(ranges[0].count, 3);
    EXPECT_EQ(ranges[1].startAddress, 200);
    EXPECT_EQ(ranges[1].count, 1);
}

TEST(AddressPackerTest, Pack_WithGap_LargeMaxGap_Merged) {
    // {100, 200} gap=99, maxGap=100 → 合并为一个区间
    auto ranges = AddressPacker::pack({100, 200}, 100);
    ASSERT_EQ(ranges.size(), 1);
    EXPECT_EQ(ranges[0].startAddress, 100);
    EXPECT_EQ(ranges[0].count, 101); // 200-100+1 = 101
}

TEST(AddressPackerTest, Pack_ThreeSegments) {
    // {10, 11, 12}, {20}, {30, 31}
    auto ranges = AddressPacker::pack({10, 11, 12, 20, 30, 31}, 0);
    ASSERT_EQ(ranges.size(), 3);
    EXPECT_EQ(ranges[0].startAddress, 10);  EXPECT_EQ(ranges[0].count, 3);
    EXPECT_EQ(ranges[1].startAddress, 20);  EXPECT_EQ(ranges[1].count, 1);
    EXPECT_EQ(ranges[2].startAddress, 30);  EXPECT_EQ(ranges[2].count, 2);
}

TEST(AddressPackerTest, Pack_UnsortedInput_RequiresPreSorted) {
    // 约定：调用方负责排序，pack 不排序
    // 未排序输入会产生意外结果，此处验证行为
    auto ranges = AddressPacker::pack({30, 10, 20}, 0);
    // 每个元素间隔都 > 0 → 各成一个区间
    EXPECT_EQ(ranges.size(), 3);
}


// ============================================================================
// Level 2: PlcPoller.prepare() 请求生成测试
// ============================================================================

class PlcPollerPrepareTest : public ::testing::Test {
protected:
    RegisterRegistry registry = makeTestRegistry();
};

TEST_F(PlcPollerPrepareTest, Prepare_GeneratesCoilRequests) {
    PlcPoller poller(registry);
    auto req = poller.prepare();

    // 4 Coil: 100,101,102,200 → maxGap=200 将 gap=97 合并 → 1 个区间 [100, 101]
    ASSERT_EQ(req.coilRequests.size(), 1);
    EXPECT_EQ(req.coilRequests[0].range.startAddress, 100);
    EXPECT_EQ(req.coilRequests[0].range.count, 101);
}

TEST_F(PlcPollerPrepareTest, Prepare_GeneratesWordRequests) {
    PlcPoller poller(registry);
    auto req = poller.prepare();

    // HoldingReg: D100(Int16), D101(Float32→占 D101+D102), D103(Int16), D200(Int16)
    // 展开地址: 100, 101, 102, 103, 200
    // maxGap=50: gap(103→200)=96 > 50 → 两个区间:
    //   [100, 4] (D100-D103), [200, 1] (D200)
    ASSERT_EQ(req.wordRequests.size(), 2);
    EXPECT_EQ(req.wordRequests[0].range.startAddress, 100);
    EXPECT_EQ(req.wordRequests[0].range.count, 4);
    EXPECT_EQ(req.wordRequests[1].range.startAddress, 200);
    EXPECT_EQ(req.wordRequests[1].range.count, 1);
}

TEST_F(PlcPollerPrepareTest, Prepare_NoCoilRegisters_EmptyCoilRequests) {
    RegisterRegistry wordOnly;
    wordOnly.addAll({
        {RegisterArea::HoldingReg, 50, RegisterType::Int16, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "D50", 0, std::nullopt},
    });

    PlcPoller poller(wordOnly);
    auto req = poller.prepare();

    EXPECT_TRUE(req.coilRequests.empty());
    ASSERT_EQ(req.wordRequests.size(), 1);
    EXPECT_EQ(req.wordRequests[0].range.startAddress, 50);
    EXPECT_EQ(req.wordRequests[0].range.count, 1);
}

TEST_F(PlcPollerPrepareTest, Prepare_NoWordRegisters_EmptyWordRequests) {
    RegisterRegistry coilOnly;
    coilOnly.addAll({
        {RegisterArea::Coil, 1, RegisterType::Bool, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "C1", 0, std::nullopt},
    });

    PlcPoller poller(coilOnly);
    auto req = poller.prepare();

    EXPECT_EQ(req.coilRequests.size(), 1);
    EXPECT_TRUE(req.wordRequests.empty());
}

TEST_F(PlcPollerPrepareTest, Prepare_EmptyRegistry_NoRequests) {
    RegisterRegistry empty;
    PlcPoller poller(empty);
    auto req = poller.prepare();

    EXPECT_TRUE(req.coilRequests.empty());
    EXPECT_TRUE(req.wordRequests.empty());
}


// ============================================================================
// Level 3: PlcPoller.assemble() 快照拼装测试
// ============================================================================

class PlcPollerAssembleTest : public ::testing::Test {
protected:
    RegisterRegistry registry = makeTestRegistry();
};

TEST_F(PlcPollerAssembleTest, Assemble_Success_Mixed) {
    PlcPoller poller(registry);

    // maxGap=200 将 Coil {100,101,102,200} 合并为单个 range [100, 101]
    // C100=0 → byte[0].bit0=0; C101=1 → byte[0].bit1=1 → byte[0]=0x02
    // C102=0 → byte[0].bit2=0
    // C200=1 → offset=100 → byteIdx=12, bit=4 → byte[12]=0x10
    std::vector<uint8_t> mergedCoilPayload(13, 0);
    mergedCoilPayload[0]  = 0x02;  // bit1=1
    mergedCoilPayload[12] = 0x10;  // bit4=1 (C200 offset=100)

    std::vector<std::vector<uint8_t>> coilResps = { mergedCoilPayload };

    // Word: maxGap=50, gap(103→200)=96 > 50 → 仍为 2 ranges
    // Range1: D100-D103 (4 words) → {1, 2, 3, 4}
    // Range2: D200 (1 word) → {99}
    std::vector<std::vector<uint16_t>> wordResps = {
        {1, 2, 3, 4},
        {99},
    };

    auto snap = poller.assemble(coilResps, wordResps, 9999);

    EXPECT_TRUE(snap.isTrusted());
    EXPECT_EQ(snap.timestamp, 9999);

    // 验证 Coil
    auto c100 = snap.bits.getBit(100);
    ASSERT_TRUE(c100.has_value());
    EXPECT_FALSE(c100.value());

    auto c101 = snap.bits.getBit(101);
    ASSERT_TRUE(c101.has_value());
    EXPECT_TRUE(c101.value());

    auto c102 = snap.bits.getBit(102);
    ASSERT_TRUE(c102.has_value());
    EXPECT_FALSE(c102.value());

    auto c200 = snap.bits.getBit(200);
    ASSERT_TRUE(c200.has_value());
    EXPECT_TRUE(c200.value());

    // 验证 Word
    auto spanD100 = snap.words.getWords(100, 1);
    ASSERT_TRUE(spanD100.has_value());
    EXPECT_EQ((*spanD100)[0], 1);

    auto spanD101 = snap.words.getWords(101, 2); // Float32 占 D101+D102
    ASSERT_TRUE(spanD101.has_value());
    EXPECT_EQ((*spanD101)[0], 2);
    EXPECT_EQ((*spanD101)[1], 3);

    auto spanD103 = snap.words.getWords(103, 1);
    ASSERT_TRUE(spanD103.has_value());
    EXPECT_EQ((*spanD103)[0], 4);

    auto spanD200 = snap.words.getWords(200, 1);
    ASSERT_TRUE(spanD200.has_value());
    EXPECT_EQ((*spanD200)[0], 99);
}

TEST_F(PlcPollerAssembleTest, Assemble_CoilResponseCountMismatch_MarkedIncomplete) {
    PlcPoller poller(registry);

    // maxGap=200 → 1 个 Coil range, 需要 1 个响应，但给了 0 个 → complete=false
    std::vector<std::vector<uint8_t>> coilResps = {};
    std::vector<std::vector<uint16_t>> wordResps = {
        {1, 2, 3, 4},
        {99},
    };

    auto snap = poller.assemble(coilResps, wordResps, 1000);
    EXPECT_FALSE(snap.isTrusted());
}

TEST_F(PlcPollerAssembleTest, Assemble_WordResponseCountMismatch_MarkedIncomplete) {
    PlcPoller poller(registry);

    // Coil: 1 range → 1 个响应 (13 字节)
    // C200=1 → offset=100 → byteIdx=12, bit=4 → byte[12]=0x10
    std::vector<uint8_t> mergedCoilPayload(13, 0);
    mergedCoilPayload[0]  = 0x02;
    mergedCoilPayload[12] = 0x10;
    std::vector<std::vector<uint8_t>> coilResps = { mergedCoilPayload };

    // Word: 2 ranges，但只给 1 个响应 → complete=false
    std::vector<std::vector<uint16_t>> wordResps = {
        {1, 2, 3, 4},
    };

    auto snap = poller.assemble(coilResps, wordResps, 1000);
    EXPECT_FALSE(snap.isTrusted());
}

TEST_F(PlcPollerAssembleTest, Assemble_CoilResponseTooShort_MarkedIncomplete) {
    PlcPoller poller(registry);

    // maxGap=200 → 1 Coil range [100,101], 需要 ceil(101/8)=13 字节，只给 3 字节
    std::vector<std::vector<uint8_t>> coilResps = {
        {0x02, 0x00, 0x00},  // 只有 3 字节 < 13 → 过短
    };
    std::vector<std::vector<uint16_t>> wordResps = {
        {1, 2, 3, 4},
        {99},
    };

    auto snap = poller.assemble(coilResps, wordResps, 1000);
    EXPECT_FALSE(snap.isTrusted());
}

TEST_F(PlcPollerAssembleTest, Assemble_WordResponseTooShort_MarkedIncomplete) {
    PlcPoller poller(registry);

    // Coil: 1 range → 1 个响应 (13 字节)
    // C200=1 → offset=100 → byteIdx=12, bit=4 → byte[12]=0x10
    std::vector<uint8_t> mergedCoilPayload(13, 0);
    mergedCoilPayload[0]  = 0x02;
    mergedCoilPayload[12] = 0x10;
    std::vector<std::vector<uint8_t>> coilResps = { mergedCoilPayload };

    // Word: Range1 需要 4 words，只给 2 个 → 过短
    std::vector<std::vector<uint16_t>> wordResps = {
        {1, 2},      // 过短
        {99},
    };

    auto snap = poller.assemble(coilResps, wordResps, 1000);
    EXPECT_FALSE(snap.isTrusted());
}

// ============================================================================
// Level 3 Plus: maxGap 跨度/间隔边界测试
// ============================================================================

// Coil maxGap=200：验证大间隙容忍
TEST_F(PlcPollerAssembleTest, Assemble_CoilLargeGapWithinMaxGap_Success) {
    // 构造 RemoteRegistry：Coil {100, 300} gap=199 ≤ maxGap=200 → 合并
    RegisterRegistry reg;
    reg.addAll({
        {RegisterArea::Coil, 100, RegisterType::Bool, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "C100", 0, std::nullopt},
        {RegisterArea::Coil, 300, RegisterType::Bool, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "C300", 0, std::nullopt},
    });

    // Coil: totalBits = 300-100+0 = 200? Wait no: range [100, 201]? Let me trace:
    // AddressPacker::pack({100, 300}, 200):
    //   i=1: gap = 300-100-1 = 199 <= 200 → merge, current.count = 300-100+1 = 201
    // So range is [100, 201], totalBits = 201
    // byteCount = ceil(201/8) = 26
    // C300 offset = 200 → byteIdx=25, bit=0 → byte[25]=0x01
    std::vector<uint8_t> coilPayload(26, 0);
    coilPayload[0]  = 0x01;   // C100=1: byte[0].bit0
    coilPayload[25] = 0x01;   // C300=1: byte[25].bit0

    PlcPoller poller(reg);
    auto snap = poller.assemble({coilPayload}, {}, 1);
    ASSERT_TRUE(snap.isTrusted());
    EXPECT_TRUE(snap.bits.getBit(100).value());
    EXPECT_TRUE(snap.bits.getBit(300).value());
}

// Coil maxGap=200：间隙超阈值 → 分裂为 2 个 range，需要 2 个响应
TEST_F(PlcPollerAssembleTest, Assemble_CoilGapExceedsMaxGap_Split) {
    // Coil {100, 302} → gap=302-100-1=201 > maxGap=200 → 分裂为 [100,1] + [302,1]
    RegisterRegistry reg;
    reg.addAll({
        {RegisterArea::Coil, 100, RegisterType::Bool, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "C100", 0, std::nullopt},
        {RegisterArea::Coil, 302, RegisterType::Bool, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "C302", 0, std::nullopt},
    });

    // 2 个 range → 2 个 Coil 响应
    std::vector<std::vector<uint8_t>> coilResps = {
        {0x01},  // C100=1
        {0x01},  // C302=1
    };

    PlcPoller poller(reg);
    auto snap = poller.assemble(coilResps, {}, 500);
    ASSERT_TRUE(snap.isTrusted());
    EXPECT_TRUE(snap.bits.getBit(100).value());
    EXPECT_TRUE(snap.bits.getBit(302).value());
}

// Word maxGap=50：间隙 49 ≤ 50 → 合并为 1 个 range
TEST_F(PlcPollerAssembleTest, Assemble_WordGapWithinMaxGap_Merged) {
    RegisterRegistry reg;
    reg.addAll({
        {RegisterArea::HoldingReg, 100, RegisterType::Int16, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "D100", 0, std::nullopt},
        {RegisterArea::HoldingReg, 150, RegisterType::Int16, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "D150", 0, std::nullopt},
    });
    // gap=150-100-1=49 ≤ 50 → 合并为 range [100, 51], totalWords=51
    // D100=10, D150=20 (offset=50)
    std::vector<uint16_t> mergedPayload(51, 0);
    mergedPayload[0]  = 10;
    mergedPayload[50] = 20;

    PlcPoller poller(reg);
    auto snap = poller.assemble({}, {mergedPayload}, 100);
    ASSERT_TRUE(snap.isTrusted());
    EXPECT_EQ((*snap.words.getWords(100, 1))[0], 10);
    EXPECT_EQ((*snap.words.getWords(150, 1))[0], 20);
}

// Word maxGap=50：间隙 51 > 50 → 分裂为 2 个 range
TEST_F(PlcPollerAssembleTest, Assemble_WordGapExceedsMaxGap_Split) {
    RegisterRegistry reg;
    reg.addAll({
        {RegisterArea::HoldingReg, 100, RegisterType::Int16, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "D100", 0, std::nullopt},
        {RegisterArea::HoldingReg, 152, RegisterType::Int16, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "D152", 0, std::nullopt},
    });
    // gap=152-100-1=51 > 50 → 分裂为 [100,1] + [152,1]

    PlcPoller poller(reg);
    auto snap = poller.assemble({}, {{10}, {20}}, 200);
    ASSERT_TRUE(snap.isTrusted());
    EXPECT_EQ((*snap.words.getWords(100, 1))[0], 10);
    EXPECT_EQ((*snap.words.getWords(152, 1))[0], 20);
}

// Coil maxGap 精确边界：gap=200（等于阈值）→ 合并
TEST_F(PlcPollerAssembleTest, Assemble_CoilGapExactlyMaxGap_Merged) {
    RegisterRegistry reg;
    reg.addAll({
        {RegisterArea::Coil, 100, RegisterType::Bool, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "C100", 0, std::nullopt},
        {RegisterArea::Coil, 301, RegisterType::Bool, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "C301", 0, std::nullopt},
    });
    // gap=301-100-1=200 == maxGap=200 → 合并为 [100, 202]

    std::vector<uint8_t> coilPayload(26, 0); // ceil(202/8)=26
    coilPayload[0]  = 0x01;  // C100=1: byte[0].bit0
    coilPayload[25] = 0x02;  // C301 offset=201 → byteIdx=25, bit=1 → 0x02

    PlcPoller poller(reg);
    auto snap = poller.assemble({coilPayload}, {}, 300);
    ASSERT_TRUE(snap.isTrusted());
    EXPECT_TRUE(snap.bits.getBit(100).value());
    EXPECT_TRUE(snap.bits.getBit(301).value());
}

// Word maxGap 精确边界：gap=50（等于阈值）→ 合并
TEST_F(PlcPollerAssembleTest, Assemble_WordGapExactlyMaxGap_Merged) {
    RegisterRegistry reg;
    reg.addAll({
        {RegisterArea::HoldingReg, 100, RegisterType::Int16, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "D100", 0, std::nullopt},
        {RegisterArea::HoldingReg, 151, RegisterType::Int16, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "D151", 0, std::nullopt},
    });
    // gap=151-100-1=50 == maxGap=50 → 合并为 [100, 52]

    std::vector<uint16_t> mergedPayload(52, 0);
    mergedPayload[0]  = 100;
    mergedPayload[51] = 200;

    PlcPoller poller(reg);
    auto snap = poller.assemble({}, {mergedPayload}, 400);
    ASSERT_TRUE(snap.isTrusted());
    EXPECT_EQ((*snap.words.getWords(100, 1))[0], 100);
    EXPECT_EQ((*snap.words.getWords(151, 1))[0], 200);
}

TEST_F(PlcPollerAssembleTest, Assemble_OnlyCoil_NoWords_Success) {
    RegisterRegistry coilOnly;
    coilOnly.addAll({
        {RegisterArea::Coil, 50, RegisterType::Bool, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "C50", 0, std::nullopt},
        {RegisterArea::Coil, 51, RegisterType::Bool, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "C51", 0, std::nullopt},
    });

    PlcPoller poller(coilOnly);
    std::vector<std::vector<uint8_t>> coilResps = {{0x03}}; // both true
    std::vector<std::vector<uint16_t>> wordResps = {};       // no FC03

    auto snap = poller.assemble(coilResps, wordResps, 500);
    EXPECT_TRUE(snap.isTrusted());

    EXPECT_TRUE(snap.bits.getBit(50).value());
    EXPECT_TRUE(snap.bits.getBit(51).value());
    EXPECT_FALSE(snap.words.getWords(0, 1).has_value());
}

TEST_F(PlcPollerAssembleTest, Assemble_OnlyWords_NoCoils_Success) {
    RegisterRegistry wordOnly;
    wordOnly.addAll({
        {RegisterArea::HoldingReg, 10, RegisterType::Int16, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "D10", 0, std::nullopt},
    });

    PlcPoller poller(wordOnly);
    std::vector<std::vector<uint8_t>> coilResps = {};
    std::vector<std::vector<uint16_t>> wordResps = {{42}};

    auto snap = poller.assemble(coilResps, wordResps, 700);
    EXPECT_TRUE(snap.isTrusted());

    EXPECT_FALSE(snap.bits.getBit(0).has_value());
    auto span = snap.words.getWords(10, 1);
    ASSERT_TRUE(span.has_value());
    EXPECT_EQ((*span)[0], 42);
}


// ============================================================================
// PlcPoller::trust() 快速组装（受信快照）
// ============================================================================

TEST(PlcPollerTrustTest, Trust_CreatesCompleteSnapshot) {
    auto snap = PlcPoller::trust(
        std::vector<uint8_t>{0x0F},      // Coil 0-3: all true
        0, 4,
        std::vector<uint16_t>{100, 200},  // D0,D1
        0,
        1234
    );

    EXPECT_TRUE(snap.isTrusted());
    EXPECT_EQ(snap.timestamp, 1234);

    EXPECT_TRUE(snap.bits.getBit(0).value());
    EXPECT_TRUE(snap.bits.getBit(3).value());
    auto span = snap.words.getWords(0, 2);
    ASSERT_TRUE(span.has_value());
    EXPECT_EQ((*span)[0], 100);
    EXPECT_EQ((*span)[1], 200);
}

TEST(PlcPollerTrustTest, Trust_EmptyBitPayload) {
    auto snap = PlcPoller::trust(
        std::vector<uint8_t>{}, 100, 0,
        std::vector<uint16_t>{7},
        200,
        0
    );

    EXPECT_TRUE(snap.isTrusted());
    EXPECT_FALSE(snap.bits.getBit(100).has_value());
    auto span = snap.words.getWords(200, 1);
    ASSERT_TRUE(span.has_value());
    EXPECT_EQ((*span)[0], 7);
}


// ============================================================================
// PlcPoller::untrusted() 不受信快照
// ============================================================================

TEST(PlcPollerUntrustedTest, Untrusted_CreatesIncompleteSnapshot) {
    auto snap = PlcPoller::untrusted(5678);

    EXPECT_FALSE(snap.isTrusted());
    EXPECT_EQ(snap.timestamp, 5678);
    EXPECT_FALSE(snap.bits.getBit(0).has_value());
    EXPECT_FALSE(snap.words.getWords(0, 1).has_value());
}


// ============================================================================
// 集成用例: prepare → (模拟 driver) → assemble 全链路
// ============================================================================

TEST(PlcPollerIntegrationTest, PrepareThenAssemble_Simple) {
    RegisterRegistry reg;
    reg.addAll({
        {RegisterArea::Coil,       0, RegisterType::Bool,  RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "Running", 0, std::nullopt},
        {RegisterArea::HoldingReg, 0, RegisterType::Int16, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "Speed", 0, std::nullopt},
        {RegisterArea::HoldingReg, 1, RegisterType::Int16, RegisterAccess::ReadOnly,
         RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "Torque", 0, std::nullopt},
    });

    PlcPoller poller(reg);
    auto req = poller.prepare();

    // 模拟 driver 执行 FC01（1 个 Coil 请求）和 FC03（1 个 Word 请求）
    // Coil 0 = true → payload = 0x01
    std::vector<std::vector<uint8_t>> coilResps = {{0x01}};
    // Word 0,1 = {100, 50}
    std::vector<std::vector<uint16_t>> wordResps = {{100, 50}};

    auto snap = poller.assemble(coilResps, wordResps, 42);

    EXPECT_TRUE(snap.isTrusted());
    EXPECT_EQ(snap.timestamp, 42);
    EXPECT_TRUE(snap.bits.getBit(0).value());
    EXPECT_EQ((*snap.words.getWords(0, 1))[0], 100);
    EXPECT_EQ((*snap.words.getWords(1, 1))[0], 50);
}
