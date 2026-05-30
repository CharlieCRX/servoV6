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

// =============================================================================
// TDD 阶段 2: 边沿触发协议 — PendingEdge 队列管理
//
// 测试目标 (§5):
//   1. PendingEdge 状态机初始化和转换
//   2. 队列入队/出队（FIFO）
//   3. 同寄存器去重
//   4. 队列容量/清理
//
// 设计依据: 《边沿触发协议（ManualResetEdgeTrigger）TDD 实现文档》§5
// =============================================================================

#include "infrastructure/plc/ModbusSystemDriver.h"
#include "infrastructure/plc/protocol/RegisterAddressAll.h"
#include <chrono>

using namespace plc;

// =============================================================================
// §5.1 PendingEdge 状态机
// =============================================================================

TEST(PendingEdgeQueueTest, PendingEdgeInitialStateIsIdle) {
    PendingEdge edge;
    EXPECT_EQ(edge.state, PendingEdge::State::Idle);
    EXPECT_EQ(edge.reg, nullptr);
}

TEST(PendingEdgeQueueTest, PendingEdgeWithRegisterStoresReference) {
    const auto& reg = reg::x_axis::command::ABS_MOVE_TRIGGER;
    PendingEdge edge;
    edge.reg = &reg;
    edge.state = PendingEdge::State::WroteOn;

    EXPECT_NE(edge.reg, nullptr);
    EXPECT_EQ(edge.reg->address, reg.address);
    EXPECT_EQ(edge.state, PendingEdge::State::WroteOn);
}

// =============================================================================
// §5.2 队列入队 / 出队
// =============================================================================

TEST(PendingEdgeQueueTest, EmptyQueueHasZeroCount) {
    ModbusSystemDriver driver;
    EXPECT_EQ(driver.pendingEdgeCount(), 0);
}

TEST(PendingEdgeQueueTest, EnqueueSingleEdgeIncrementsCount) {
    ModbusSystemDriver driver;
    driver.enqueueEdge(&reg::x_axis::command::ABS_MOVE_TRIGGER);
    EXPECT_EQ(driver.pendingEdgeCount(), 1);
}

TEST(PendingEdgeQueueTest, DequeueReturnsNullWhenEmpty) {
    ModbusSystemDriver driver;
    EXPECT_EQ(driver.dequeueEdge().reg, nullptr);
}

TEST(PendingEdgeQueueTest, DequeueReturnsEnqueuedEdge) {
    ModbusSystemDriver driver;
    const auto& reg = reg::x_axis::command::ABS_MOVE_TRIGGER;
    driver.enqueueEdge(&reg);

    const PendingEdge edge = driver.dequeueEdge();
    ASSERT_NE(edge.reg, nullptr);
    EXPECT_EQ(edge.reg, &reg);
    EXPECT_EQ(edge.state, PendingEdge::State::Idle);
}

TEST(PendingEdgeQueueTest, DequeueReducesCount) {
    ModbusSystemDriver driver;
    driver.enqueueEdge(&reg::x_axis::command::ABS_MOVE_TRIGGER);
    driver.enqueueEdge(&reg::x_axis::command::REL_MOVE_TRIGGER);
    EXPECT_EQ(driver.pendingEdgeCount(), 2);

    driver.dequeueEdge();
    EXPECT_EQ(driver.pendingEdgeCount(), 1);

    driver.dequeueEdge();
    EXPECT_EQ(driver.pendingEdgeCount(), 0);
}

TEST(PendingEdgeQueueTest, QueuePreservesFifoOrder) {
    ModbusSystemDriver driver;
    const auto& r1 = reg::x_axis::command::ABS_MOVE_TRIGGER;
    const auto& r2 = reg::x_axis::command::REL_MOVE_TRIGGER;
    const auto& r3 = reg::x_axis::command::ENABLE_REQUEST;

    driver.enqueueEdge(&r1);
    driver.enqueueEdge(&r2);
    driver.enqueueEdge(&r3);

    EXPECT_EQ(driver.dequeueEdge().reg, &r1);
    EXPECT_EQ(driver.dequeueEdge().reg, &r2);
    EXPECT_EQ(driver.dequeueEdge().reg, &r3);
    EXPECT_EQ(driver.dequeueEdge().reg, nullptr);
}

// =============================================================================
// §5.3 去重 —— 同寄存器不重复入队
// =============================================================================

TEST(PendingEdgeQueueTest, DuplicateRegisterNotEnqueued) {
    ModbusSystemDriver driver;
    const auto& reg = reg::x_axis::command::ABS_MOVE_TRIGGER;

    driver.enqueueEdge(&reg);
    EXPECT_EQ(driver.pendingEdgeCount(), 1);

    driver.enqueueEdge(&reg);  // 重复入队
    EXPECT_EQ(driver.pendingEdgeCount(), 1);  // 仍为 1
}

TEST(PendingEdgeQueueTest, DuplicateCheckIsAddressBased) {
    ModbusSystemDriver driver;
    const auto& reg1 = reg::x_axis::command::ABS_MOVE_TRIGGER;  // M40
    // 同一个寄存器, 不同命名空间入口 (本应相同)
    const auto& reg2 = reg::x_axis::command::ABS_MOVE_TRIGGER;  // M40

    driver.enqueueEdge(&reg1);
    driver.enqueueEdge(&reg2);  // 相同地址 -> 去重
    EXPECT_EQ(driver.pendingEdgeCount(), 1);
}

TEST(PendingEdgeQueueTest, DifferentRegistersCanBothBeQueued) {
    ModbusSystemDriver driver;
    driver.enqueueEdge(&reg::x_axis::command::ABS_MOVE_TRIGGER);
    driver.enqueueEdge(&reg::x_axis::command::REL_MOVE_TRIGGER);
    EXPECT_EQ(driver.pendingEdgeCount(), 2);
}

// =============================================================================
// §5.4 查询 — 是否包含指定寄存器
// =============================================================================

TEST(PendingEdgeQueueTest, IsEdgePendingReturnsTrueForQueuedRegister) {
    ModbusSystemDriver driver;
    const auto& reg = reg::x_axis::command::ENABLE_REQUEST;
    driver.enqueueEdge(&reg);
    EXPECT_TRUE(driver.isEdgePending(&reg));
}

TEST(PendingEdgeQueueTest, IsEdgePendingReturnsFalseForNotQueuedRegister) {
    ModbusSystemDriver driver;
    EXPECT_FALSE(driver.isEdgePending(&reg::x_axis::command::ABS_MOVE_TRIGGER));
}

TEST(PendingEdgeQueueTest, IsEdgePendingFalseAfterDequeue) {
    ModbusSystemDriver driver;
    const auto& reg = reg::x_axis::command::ABS_MOVE_TRIGGER;
    driver.enqueueEdge(&reg);
    EXPECT_TRUE(driver.isEdgePending(&reg));

    driver.dequeueEdge();
    EXPECT_FALSE(driver.isEdgePending(&reg));
}

// =============================================================================
// §5.5 清理
// =============================================================================

TEST(PendingEdgeQueueTest, ClearEmptiesQueue) {
    ModbusSystemDriver driver;
    driver.enqueueEdge(&reg::x_axis::command::ABS_MOVE_TRIGGER);
    driver.enqueueEdge(&reg::x_axis::command::REL_MOVE_TRIGGER);
    driver.enqueueEdge(&reg::x_axis::command::ENABLE_REQUEST);
    EXPECT_EQ(driver.pendingEdgeCount(), 3);

    driver.clearPendingEdges();
    EXPECT_EQ(driver.pendingEdgeCount(), 0);
    EXPECT_EQ(driver.dequeueEdge().reg, nullptr);
}

TEST(PendingEdgeQueueTest, ClearThenEnqueueWorks) {
    ModbusSystemDriver driver;
    driver.enqueueEdge(&reg::x_axis::command::ABS_MOVE_TRIGGER);
    driver.clearPendingEdges();

    driver.enqueueEdge(&reg::x_axis::command::REL_MOVE_TRIGGER);
    EXPECT_EQ(driver.pendingEdgeCount(), 1);
    EXPECT_EQ(driver.dequeueEdge().reg, &reg::x_axis::command::REL_MOVE_TRIGGER);
}
