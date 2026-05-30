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

// =============================================================================
// TDD 阶段 3: sendEdgeTrigger — 写入 ON 并入队
//
// 测试目标 (§6):
//   1. sendEdgeTrigger 成功时写入 ON，并入队（WroteOn 状态 + 记录 onTime）
//   2. 无 PlcDevice 时返回 Disconnected
//   3. 写入失败时返回失败结果，不入队（避免队列泄漏）
//   4. 入队项保存正确的寄存器引用
//
// 设计依据: 《边沿触发协议（ManualResetEdgeTrigger）TDD 实现文档》§6
// =============================================================================

#include "infrastructure/plc/protocol/PlcDevice.h"
#include "infrastructure/plc/protocol/ProtocolProfile.h"

using namespace plc::protocol;

// =============================================================================
// 测试辅助：FakeModbusClient — 可按需配置通讯成功/失败
// =============================================================================

namespace {

class FakeModbusClientForEdgeTrigger : public IModbusClient {
public:
    CommunicationResult nextWriteResult = CommunicationResult{CommunicationResult::Status::Sent};

    int coilWriteCount = 0;
    uint16_t lastCoilAddress = 0;
    bool lastCoilValue = false;

    CommunicationResult readCoils(uint16_t, uint16_t, std::vector<uint8_t>&) override {
        return CommunicationResult{CommunicationResult::Status::Sent};
    }

    CommunicationResult readHoldingRegisters(uint16_t, uint16_t, std::vector<uint16_t>&) override {
        return CommunicationResult{CommunicationResult::Status::Sent};
    }

    CommunicationResult writeSingleCoil(uint16_t address, bool value) override {
        coilWriteCount++;
        lastCoilAddress = address;
        lastCoilValue = value;
        return nextWriteResult;
    }

    CommunicationResult writeSingleRegister(uint16_t, uint16_t) override {
        return CommunicationResult{CommunicationResult::Status::Sent};
    }

    CommunicationResult writeMultipleRegisters(uint16_t, const std::vector<uint16_t>&) override {
        return CommunicationResult{CommunicationResult::Status::Sent};
    }
};

constexpr ProtocolProfile TEST_PROFILE_ET = {
    "Test_EdgeTrigger",
    {ByteOrder::BigEndian, WordOrder::LowWordFirst},
    120, 120, true, false
};

} // anonymous namespace

// =============================================================================
// §6.1 成功路径：写入 ON 并入队
// =============================================================================

class SendEdgeTriggerTest : public ::testing::Test {
protected:
    FakeModbusClientForEdgeTrigger fakeClient;
    PlcDevice device{TEST_PROFILE_ET};
    ModbusSystemDriver driver;

    void SetUp() override {
        device.bindTransport(&fakeClient);
        driver.setDevice(&device);
    }
};

TEST_F(SendEdgeTriggerTest, SuccessfulSendWritesOnAndEnqueues) {
    const auto& reg = reg::x_axis::command::ABS_MOVE_TRIGGER;

    CommunicationResult result = driver.sendEdgeTrigger(reg);

    // 1. 通讯成功
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::Sent);

    // 2. 确实调用了 writeSingleCoil(address, true)
    EXPECT_EQ(fakeClient.coilWriteCount, 1);
    EXPECT_EQ(fakeClient.lastCoilAddress, reg.address);
    EXPECT_TRUE(fakeClient.lastCoilValue);

    // 3. 队列中有一项
    EXPECT_EQ(driver.pendingEdgeCount(), 1);

    // 4. 队列项状态为 WroteOn
    PendingEdge edge = driver.dequeueEdge();
    ASSERT_NE(edge.reg, nullptr);
    EXPECT_EQ(edge.reg->address, reg.address);
    EXPECT_EQ(edge.state, PendingEdge::State::WroteOn);
}

TEST_F(SendEdgeTriggerTest, EnqueuedEdgeRecordsOnTime) {
    const auto& reg = reg::x_axis::command::ABS_MOVE_TRIGGER;

    // 注入 FakeClock 以控制时间
    auto fakeClock = std::make_unique<FakeClock>();
    auto* rawClock = fakeClock.get();
    driver.setClock(std::move(fakeClock));

    // 推进到 100ms 后发送
    rawClock->advance(std::chrono::milliseconds(100));
    driver.sendEdgeTrigger(reg);

    PendingEdge edge = driver.dequeueEdge();
    ASSERT_NE(edge.reg, nullptr);

    // onTime 应为 100ms（从 epoch 算起）
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        edge.onTime.time_since_epoch()
    ).count();
    EXPECT_EQ(elapsed, 100);
}

TEST_F(SendEdgeTriggerTest, MultipleSuccessfulSendsAllEnqueued) {
    const auto& r1 = reg::x_axis::command::ABS_MOVE_TRIGGER;
    const auto& r2 = reg::x_axis::command::REL_MOVE_TRIGGER;
    const auto& r3 = reg::x_axis::command::ENABLE_REQUEST;

    driver.sendEdgeTrigger(r1);
    driver.sendEdgeTrigger(r2);
    driver.sendEdgeTrigger(r3);

    EXPECT_EQ(driver.pendingEdgeCount(), 3);
    EXPECT_EQ(fakeClient.coilWriteCount, 3);

    // FIFO 顺序
    PendingEdge e1 = driver.dequeueEdge();
    PendingEdge e2 = driver.dequeueEdge();
    PendingEdge e3 = driver.dequeueEdge();

    EXPECT_EQ(e1.reg->address, r1.address);
    EXPECT_EQ(e2.reg->address, r2.address);
    EXPECT_EQ(e3.reg->address, r3.address);

    EXPECT_EQ(e1.state, PendingEdge::State::WroteOn);
    EXPECT_EQ(e2.state, PendingEdge::State::WroteOn);
    EXPECT_EQ(e3.state, PendingEdge::State::WroteOn);
}

// =============================================================================
// §6.2 失败路径：无 PlcDevice
// =============================================================================

TEST(SendEdgeTriggerNoDeviceTest, ReturnsDisconnectedWhenNoDevice) {
    ModbusSystemDriver driver;
    // 未调用 setDevice()

    const auto& reg = reg::x_axis::command::ABS_MOVE_TRIGGER;
    CommunicationResult result = driver.sendEdgeTrigger(reg);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::Disconnected);
    EXPECT_EQ(driver.pendingEdgeCount(), 0);  // 未入队
}

// =============================================================================
// §6.3 失败路径：写入失败不入队
// =============================================================================

class SendEdgeTriggerFailureTest : public ::testing::Test {
protected:
    FakeModbusClientForEdgeTrigger fakeClient;
    PlcDevice device{TEST_PROFILE_ET};
    ModbusSystemDriver driver;

    void SetUp() override {
        device.bindTransport(&fakeClient);
        driver.setDevice(&device);
    }
};

TEST_F(SendEdgeTriggerFailureTest, WriteTimeoutDoesNotEnqueue) {
    fakeClient.nextWriteResult = CommunicationResult{
        CommunicationResult::Status::Timeout, 0, "write timeout"
    };

    const auto& reg = reg::x_axis::command::ABS_MOVE_TRIGGER;
    CommunicationResult result = driver.sendEdgeTrigger(reg);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::Timeout);
    EXPECT_TRUE(result.retryable());

    // 队列没有增长
    EXPECT_EQ(driver.pendingEdgeCount(), 0);
    EXPECT_EQ(fakeClient.coilWriteCount, 1);  // 尝试写入但失败
}

TEST_F(SendEdgeTriggerFailureTest, WriteNetworkErrorDoesNotEnqueue) {
    fakeClient.nextWriteResult = CommunicationResult{
        CommunicationResult::Status::NetworkError, 0, "ECONNRESET"
    };

    const auto& reg = reg::x_axis::command::ENABLE_REQUEST;
    CommunicationResult result = driver.sendEdgeTrigger(reg);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::NetworkError);
    EXPECT_FALSE(result.retryable());

    EXPECT_EQ(driver.pendingEdgeCount(), 0);
}

TEST_F(SendEdgeTriggerFailureTest, WriteFailureThenSuccessOnlyEnqueuesSuccess) {
    const auto& reg = reg::x_axis::command::ABS_MOVE_TRIGGER;

    // 第一次写入失败
    fakeClient.nextWriteResult = CommunicationResult{
        CommunicationResult::Status::Timeout, 0, "timeout"
    };
    auto r1 = driver.sendEdgeTrigger(reg);
    EXPECT_FALSE(r1.ok());
    EXPECT_EQ(driver.pendingEdgeCount(), 0);

    // 第二次写入成功
    fakeClient.nextWriteResult = CommunicationResult{CommunicationResult::Status::Sent};
    auto r2 = driver.sendEdgeTrigger(reg);
    EXPECT_TRUE(r2.ok());
    EXPECT_EQ(driver.pendingEdgeCount(), 1);
}

// =============================================================================
// TDD 阶段 4: servicePendingEdgeTriggers — 扫描并写入 OFF
//
// 测试目标 (§7.3):
//   1. SingleEdgeTriggerFullLifecycle — ON → 150ms → OFF 完整生命周期
//   2. MultipleConcurrentEdgeTriggersIndependentTiming — 多个并发独立计时
//   3. SameRegisterTriggeredTwiceSequentially — 同一寄存器连续触发
//
// 设计依据: 《边沿触发协议（ManualResetEdgeTrigger）TDD 实现文档》§7
// =============================================================================

class ServicePendingEdgeTriggersTest : public ::testing::Test {
protected:
    FakeModbusClientForEdgeTrigger fakeClient;
    PlcDevice device{TEST_PROFILE_ET};
    ModbusSystemDriver driver;

    void SetUp() override {
        device.bindTransport(&fakeClient);
        driver.setDevice(&device);
    }

    /// 辅助：注入 FakeClock 并返回原始指针，用于后续 advance 控制时间
    FakeClock* injectFakeClock() {
        auto fakeClock = std::make_unique<FakeClock>();
        auto* raw = fakeClock.get();
        driver.setClock(std::move(fakeClock));
        return raw;
    }
};

// =============================================================================
// §7.3.1 单次 EdgeTrigger 完整生命周期：ON → 150ms → OFF
// =============================================================================

TEST_F(ServicePendingEdgeTriggersTest, SingleEdgeTriggerFullLifecycle) {
    const auto& reg = reg::x_axis::command::CLEAR_ABS_POS;  // M30
    auto* clock = injectFakeClock();

    // Step 1: 发送 ON 脉冲
    CommunicationResult result = driver.sendEdgeTrigger(reg);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver.pendingEdgeCount(), 1);

    // 验证 ON 已写入
    EXPECT_EQ(fakeClient.coilWriteCount, 1);
    EXPECT_TRUE(fakeClient.lastCoilValue);  // ON = true

    // Step 2: 推进 100ms — 脉冲未到期，不应写入 OFF
    clock->advance(std::chrono::milliseconds(100));
    int coilCountBeforeService = fakeClient.coilWriteCount;
    driver.servicePendingEdgeTriggers();

    // 队列仍保留（脉冲未到期）
    EXPECT_EQ(driver.pendingEdgeCount(), 1);
    // 不应有新的写入
    EXPECT_EQ(fakeClient.coilWriteCount, coilCountBeforeService);

    // Step 3: 推进到 150ms — 脉冲到期，应写入 OFF
    clock->advance(std::chrono::milliseconds(50));  // 累计 150ms
    driver.servicePendingEdgeTriggers();

    // OFF 已写入
    EXPECT_EQ(fakeClient.coilWriteCount, 2);
    EXPECT_FALSE(fakeClient.lastCoilValue);  // OFF = false
    EXPECT_EQ(fakeClient.lastCoilAddress, reg.address);

    // 队列已清空
    EXPECT_EQ(driver.pendingEdgeCount(), 0);
}

// =============================================================================
// §7.3.2 多个并发 EdgeTrigger 独立计时
// =============================================================================

TEST_F(ServicePendingEdgeTriggersTest, MultipleConcurrentEdgeTriggersIndependentTiming) {
    const auto& regA = reg::x_axis::command::CLEAR_ABS_POS;   // M30
    const auto& regB = reg::y_axis::command::CLEAR_ABS_POS;   // M31
    auto* clock = injectFakeClock();

    // T=0: 写入 regA ON
    driver.sendEdgeTrigger(regA);
    EXPECT_EQ(driver.pendingEdgeCount(), 1);

    // T=10: 写入 regB ON
    clock->advance(std::chrono::milliseconds(10));
    driver.sendEdgeTrigger(regB);
    EXPECT_EQ(driver.pendingEdgeCount(), 2);
    EXPECT_EQ(fakeClient.coilWriteCount, 2);  // 两次 ON 写入

    // T=150: regA 到期 (T=0 + 150 = 150), regB 还差 10ms (T=10 + 150 = 160)
    clock->advance(std::chrono::milliseconds(140));  // 累计 T=150
    driver.servicePendingEdgeTriggers();

    // regA 应被写入 OFF，regB 不应被写入 OFF
    EXPECT_EQ(fakeClient.coilWriteCount, 3);  // 仅新增 1 次 OFF
    EXPECT_FALSE(fakeClient.lastCoilValue);   // OFF = false
    EXPECT_EQ(fakeClient.lastCoilAddress, regA.address);

    // 队列还剩 regB
    EXPECT_EQ(driver.pendingEdgeCount(), 1);

    // T=160: regB 也到期
    clock->advance(std::chrono::milliseconds(10));  // 累计 T=160
    driver.servicePendingEdgeTriggers();

    // regB 应被写入 OFF
    EXPECT_EQ(fakeClient.coilWriteCount, 4);  // 再次新增 1 次 OFF
    EXPECT_FALSE(fakeClient.lastCoilValue);
    EXPECT_EQ(fakeClient.lastCoilAddress, regB.address);

    // 队列清空
    EXPECT_EQ(driver.pendingEdgeCount(), 0);
}

// =============================================================================
// §7.3.3 同一寄存器连续两次触发（覆盖前一个）
// =============================================================================

TEST_F(ServicePendingEdgeTriggersTest, SameRegisterTriggeredTwiceSequentially) {
    const auto& reg = reg::x_axis::command::REL_MOVE_TRIGGER;  // M41
    auto* clock = injectFakeClock();

    // 第一次触发：T=0
    driver.sendEdgeTrigger(reg);
    EXPECT_EQ(driver.pendingEdgeCount(), 1);
    EXPECT_EQ(fakeClient.coilWriteCount, 1);  // 第 1 次 ON

    // T=50: 第一次还没到期，第二次触发
    clock->advance(std::chrono::milliseconds(50));
    driver.sendEdgeTrigger(reg);
    EXPECT_EQ(driver.pendingEdgeCount(), 2);  // 两个独立的 PendingEdge
    EXPECT_EQ(fakeClient.coilWriteCount, 2);  // 第 2 次 ON

    // T=200: 第一个到期（T=0 + 150 = 150 <= 200），第二个也到期（T=50 + 150 = 200 <= 200）
    clock->advance(std::chrono::milliseconds(150));  // 累计 T=200
    driver.servicePendingEdgeTriggers();

    // 两个都应被写入 OFF
    EXPECT_EQ(fakeClient.coilWriteCount, 4);  // 新增 2 次 OFF
    EXPECT_FALSE(fakeClient.lastCoilValue);

    // 队列清空
    EXPECT_EQ(driver.pendingEdgeCount(), 0);
}
