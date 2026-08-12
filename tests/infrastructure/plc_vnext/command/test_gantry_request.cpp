// ============================================================================
// test_gantry_request.cpp —— Step 9 command: GantryRequest 契约（红）
// ============================================================================
// 依据《TDD实施文档》Step 9 与《PLC龙门联动控制逻辑》§3.1/§3.5：
//   一个龙门请求 = 组事务（SYN0 + X1 + X2），经 Command+RequestSeq 提交。
//   Couple=1 / Decouple=2 / Reset=3；requestSeq 为 application 提供的 N+1。
// 纯 DTO 契约测试，不涉及任何 I/O。
// ============================================================================
#include <cstdint>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/contracts/GantryRequest.h"

namespace plc_vnext::contracts {
namespace {

TEST(GantryRequestTest, Couple_FactoryCarriesSeqAndCode1) {
    auto r = GantryRequest::couple(7);  // N=6 → N+1=7
    EXPECT_EQ(r.command, GantryCommandKind::Couple);
    EXPECT_EQ(r.commandCode(), 1);
    EXPECT_EQ(r.requestSeq, 7);
}

TEST(GantryRequestTest, Decouple_FactoryCarriesSeqAndCode2) {
    auto r = GantryRequest::decouple(8);
    EXPECT_EQ(r.command, GantryCommandKind::Decouple);
    EXPECT_EQ(r.commandCode(), 2);
    EXPECT_EQ(r.requestSeq, 8);
}

TEST(GantryRequestTest, Reset_FactoryCarriesSeqAndCode3) {
    auto r = GantryRequest::reset(9);
    EXPECT_EQ(r.command, GantryCommandKind::Reset);
    EXPECT_EQ(r.commandCode(), 3);
    EXPECT_EQ(r.requestSeq, 9);
}

TEST(GantryRequestTest, Default_IsNoneCode0) {
    GantryRequest r;
    EXPECT_EQ(r.command, GantryCommandKind::None);
    EXPECT_EQ(r.commandCode(), 0);
    EXPECT_EQ(r.requestSeq, 0);
}

TEST(GantryRequestTest, Seq_IsApplicationProvided_NotWriterGenerated) {
    // requestSeq 由 application/session 提供，DTO 原样承载（不递增、不归一化）。
    EXPECT_EQ(GantryRequest::couple(-1).requestSeq, -1);
    EXPECT_EQ(GantryRequest::couple(INT32_MAX).requestSeq, INT32_MAX);
    EXPECT_EQ(GantryRequest::couple(INT32_MIN).requestSeq, INT32_MIN);
}

}  // namespace
}  // namespace plc_vnext::contracts
