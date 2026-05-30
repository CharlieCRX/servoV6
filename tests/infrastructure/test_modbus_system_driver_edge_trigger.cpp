// =============================================================================
// TDD 阶段 1: 边沿触发协议 — IClock 时间注入接口测试
//
// 测试目标 (§4.2):
//   1. SteadyClockReturnsRealTime — SteadyClock::now() 返回真实时间，验证单调性
//   2. FakeClockReturnsControlledTime — FakeClock::now() 返回可控时间
//   3. FakeClockAccumulatesTime — 多次 advance 的累加效果
//
// 设计依据: 《边沿触发协议（ManualResetEdgeTrigger）TDD 实现文档》§4
// =============================================================================

#include <gtest/gtest.h>
#include "infrastructure/utils/IClock.h"

// =============================================================================
// 测试套件：IClock 接口
// =============================================================================

// 测试 SteadyClock::now() 返回真实时间
TEST(IClockTest, SteadyClockReturnsRealTime) {
    SteadyClock clock;
    auto t1 = clock.now();
    // 无法直接断言时间值，但可以验证单调性
    auto t2 = clock.now();
    EXPECT_GE(t2, t1);
}

// 测试 FakeClock::now() 返回固定时间
TEST(IClockTest, FakeClockReturnsControlledTime) {
    FakeClock clock;
    auto t0 = clock.now();
    EXPECT_EQ(t0.time_since_epoch().count(), 0);  // 初始时刻为 epoch

    clock.advance(std::chrono::milliseconds(100));
    auto t1 = clock.now();
    EXPECT_EQ(
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
        100
    );
}

// 测试多次 advance 的累加效果
TEST(IClockTest, FakeClockAccumulatesTime) {
    FakeClock clock;
    clock.advance(std::chrono::milliseconds(50));
    clock.advance(std::chrono::milliseconds(50));
    clock.advance(std::chrono::milliseconds(50));

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock.now().time_since_epoch()
    ).count();
    EXPECT_EQ(elapsed, 150);
}
