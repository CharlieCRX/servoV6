// ============================================================================
// test_read_plan_builder.cpp —— Step 4 layout: 批量读计划 ReadPlanBuilder
// ============================================================================
// 目标：把"需要轮询的全部地址"合并为最少且合法的批量读取请求：
//   FC03（保持寄存器/D 区）每片 ≤ 125；FC01（线圈/M 区）每片 ≤ 2000。
//   纯函数、无 I/O；不识别槽位/轴/业务（与 tools/plc_read_validate.py 的分片
//   逻辑一致：D 区按 125 分片、M 区一次读完）。
// ============================================================================
#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <vector>

#include "infrastructure/plc_vnext/layout/ReadPlanBuilder.h"

namespace plc_vnext::layout {
namespace {

TEST(ReadPlanBuilderTest, MergesAdjacentHoldingRegions) {
    // absPosition(64..95) 与 relPosition(96..127) 相邻 → 合并为单段 [64,128)
    auto plan = ReadPlanBuilder::buildHolding({{64, 32}, {96, 32}});
    ASSERT_EQ(plan.size(), 1u);
    EXPECT_EQ(plan[0], (ReadRange{ReadArea::Holding, 64, 64}));
}

TEST(ReadPlanBuilderTest, KeepsGapSeparate) {
    // [0,4) 与 [8,3) 之间存在空隔 4..7，不得合并，各自成请求
    auto plan = ReadPlanBuilder::buildHolding({{0, 4}, {8, 3}});
    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(plan[0], (ReadRange{ReadArea::Holding, 0, 4}));
    EXPECT_EQ(plan[1], (ReadRange{ReadArea::Holding, 8, 3}));
}

TEST(ReadPlanBuilderTest, SplitsOver125Registers) {
    // [100,400) 共 300 个寄存器 → 按 125 分片：125 + 125 + 50
    auto plan = ReadPlanBuilder::buildHolding({{100, 300}});
    ASSERT_EQ(plan.size(), 3u);
    EXPECT_EQ(plan[0], (ReadRange{ReadArea::Holding, 100, 125}));
    EXPECT_EQ(plan[1], (ReadRange{ReadArea::Holding, 225, 125}));
    EXPECT_EQ(plan[2], (ReadRange{ReadArea::Holding, 350, 50}));
}

TEST(ReadPlanBuilderTest, DeduplicatesAndSorts) {
    // 乱序 + 重叠：{20,3} {5,3} {20,4}
    // → 展开去重排序 {5,6,7,20,21,22,23} → [5,8) 与 [20,24)
    auto plan = ReadPlanBuilder::buildHolding({{20, 3}, {5, 3}, {20, 4}});
    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(plan[0], (ReadRange{ReadArea::Holding, 5, 3}));
    EXPECT_EQ(plan[1], (ReadRange{ReadArea::Holding, 20, 4}));
}

TEST(ReadPlanBuilderTest, MergesPartialOverlap) {
    // {20,3} 覆盖 20..22 与 {22,4} 覆盖 22..25 部分重叠
    // → 合并为 {20,6}（覆盖 20..25），防止将来把"仅相邻合并"退化漏掉重叠
    auto plan = ReadPlanBuilder::buildHolding({{20, 3}, {22, 4}});
    ASSERT_EQ(plan.size(), 1u);
    EXPECT_EQ(plan[0], (ReadRange{ReadArea::Holding, 20, 6}));
}

TEST(ReadPlanBuilderTest, InvalidInput_Throws) {
    // 契约：count ≥ 1 —— 非法输入绝不静默忽略
    EXPECT_THROW(static_cast<void>(ReadPlanBuilder::buildHolding({{0, 0}})),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(ReadPlanBuilder::buildHolding({{0, -1}})),
                 std::invalid_argument);
    // 契约：start ≥ 0 —— 负地址
    EXPECT_THROW(static_cast<void>(ReadPlanBuilder::buildHolding({{-1, 1}})),
                 std::invalid_argument);
    // 契约：start + count 不得溢出 int（有符号溢出是 UB，必须在进入合并前拦截）
    EXPECT_THROW(static_cast<void>(ReadPlanBuilder::buildHolding(
                     {{std::numeric_limits<int>::max() - 1, 3}})),
                 std::invalid_argument);
    // 线圈区同样受契约约束
    EXPECT_THROW(static_cast<void>(ReadPlanBuilder::buildCoils({{0, 0}})),
                 std::invalid_argument);
}

TEST(ReadPlanBuilderTest, CoilsMergeIndependent) {
    // 相邻线圈区间独立合并为单请求
    auto merged = ReadPlanBuilder::buildCoils({{0, 10}, {10, 5}});
    ASSERT_EQ(merged.size(), 1u);
    EXPECT_EQ(merged[0], (ReadRange{ReadArea::Coil, 0, 15}));

    // 超过 FC01 上限（2000）自动分片：2000 + 5
    auto split = ReadPlanBuilder::buildCoils({{0, 2005}});
    ASSERT_EQ(split.size(), 2u);
    EXPECT_EQ(split[0], (ReadRange{ReadArea::Coil, 0, 2000}));
    EXPECT_EQ(split[1], (ReadRange{ReadArea::Coil, 2000, 5}));
}

TEST(ReadPlanBuilderTest, EmptyInput_ReturnsEmptyPlan) {
    EXPECT_TRUE(ReadPlanBuilder::buildHolding({}).empty());
    EXPECT_TRUE(ReadPlanBuilder::buildCoils({}).empty());
}

}  // namespace
}  // namespace plc_vnext::layout
