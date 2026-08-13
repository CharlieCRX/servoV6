// ============================================================================
// test_jog_session.cpp —— 阶段3：点动心跳会话单元测试
// ============================================================================
// 覆盖 Step 3.1 自动化验收项：
//   - 点动启动后立即写入心跳；心跳按约周期重复；
//   - 停止后心跳停止，并补写心跳 OFF；
//   - 心跳写失败后会话退出（failed），不自动重放。
// 用短周期（20ms）在离线测试中快速验证，不改变生产默认 500ms。
// ============================================================================
#include <atomic>
#include <chrono>
#include <thread>

#include "application_vnext/commissioning/JogSession.h"
#include "gtest/gtest.h"

namespace {

using application_vnext::commissioning::JogSession;

TEST(JogSessionTest, StartsAndWritesHeartbeatPeriodically) {
    std::atomic<int> beats{0};
    JogSession::Config cfg;
    cfg.heartbeatPeriodMs = 20;
    JogSession s([&]() -> bool { beats++; return true; },
                 []() -> bool { return true; },
                 []() {}, cfg);
    ASSERT_TRUE(s.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(90));
    ASSERT_TRUE(s.stop(true));
    // 立即一次 + 20ms 周期下约 4~5 次；宽松断言 >= 3。
    EXPECT_GE(beats.load(), 3);
    EXPECT_FALSE(s.active());
    EXPECT_FALSE(s.failed());
}

TEST(JogSessionTest, StopWritesHeartbeatOff) {
    std::atomic<bool> off{false};
    JogSession s([]() -> bool { return true; },
                 [&]() -> bool { off = true; return true; },
                 []() {}, JogSession::Config{});
    ASSERT_TRUE(s.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(s.stop(true));
    EXPECT_TRUE(off.load());
}

TEST(JogSessionTest, HeartbeatFailureExitsSessionAndCallsFailure) {
    std::atomic<int> beats{0};
    std::atomic<bool> onFailure{false};
    JogSession::Config cfg;
    cfg.heartbeatPeriodMs = 10;
    JogSession s([&]() -> bool { if (beats++ >= 2) return false; return true; },
                 []() -> bool { return true; },
                 [&]() { onFailure = true; }, cfg);
    ASSERT_TRUE(s.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    s.stop(false);
    EXPECT_TRUE(s.failed());
    EXPECT_FALSE(s.active());
    EXPECT_TRUE(onFailure.load());
}

}  // namespace
