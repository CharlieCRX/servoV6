// ============================================================================
// test_control_state_store.cpp —— Phase 0：统一状态存储线程安全 + 历史裁剪单测
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 0 验收：
//   - ControlStateStore::publish/snapshot/findOperation 线程安全单测通过
//     （多线程并发发布+读取）；
//   - 历史裁剪：超过 kMaxOperations 时最旧条目被移除，长期运行内存不增长。
// ============================================================================
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "application_vnext/control/ControlStateStore.h"
#include "application_vnext/control/OperationState.h"

namespace application_vnext::control {
namespace {

ControlStateSnapshot makeSnapshotWithOperations(std::size_t n) {
    ControlStateSnapshot s;
    s.connection = plc_vnext::contracts::ConnectionState::connectedState("up");
    for (std::size_t i = 0; i < n; ++i) {
        OperationEntry op;
        op.operationId = "op-" + std::to_string(i);
        op.axis = "A.Y";
        op.kind = OperationKind::Positioning;
        op.state = OperationState::Queued;
        s.operations.push_back(op);
    }
    return s;
}

TEST(ControlStateStoreTest, PublishAndSnapshotRoundTrip) {
    ControlStateStore store;
    auto snap = makeSnapshotWithOperations(2);
    snap.axes[3].absPosition = 12.5f;
    snap.axes[3].leased = true;
    snap.axes[3].leaseOwnerName = "UDP";
    snap.operations[0].state = OperationState::Running;

    store.publish(snap);

    auto out = store.snapshot();
    EXPECT_EQ(out.operations.size(), 2u);
    EXPECT_EQ(out.axes[3].absPosition, 12.5f);
    EXPECT_TRUE(out.axes[3].leased);
    EXPECT_EQ(out.axes[3].leaseOwnerName, "UDP");
    EXPECT_EQ(out.operations[0].state, OperationState::Running);
}

TEST(ControlStateStoreTest, FindOperationByOperationId) {
    ControlStateStore store;
    auto snap = makeSnapshotWithOperations(3);
    snap.operations[1].operationId = "udp-42";
    snap.operations[1].source = ControlSource::Udp;
    store.publish(snap);

    auto found = store.findOperation("udp-42");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->source, ControlSource::Udp);

    EXPECT_FALSE(store.findOperation("missing").has_value());
}

TEST(ControlStateStoreTest, HistoryIsTrimmedToMaxOperations) {
    ControlStateStore store;
    constexpr std::size_t kExtra = 25;
    const auto oversized = makeSnapshotWithOperations(ControlStateStore::kMaxOperations + kExtra);
    store.publish(oversized);

    auto out = store.snapshot();
    ASSERT_EQ(out.operations.size(), ControlStateStore::kMaxOperations);
    // 最旧条目被移除：第一条应为 "op-25"（0..24 被裁剪），最后一条为末尾
    EXPECT_EQ(out.operations.front().operationId, "op-" + std::to_string(kExtra));
    EXPECT_EQ(out.operations.back().operationId,
              "op-" + std::to_string(ControlStateStore::kMaxOperations + kExtra - 1));
}

TEST(ControlStateStoreTest, ThreadSafeConcurrentPublishAndRead) {
    ControlStateStore store;
    constexpr int kPublishers = 4;
    constexpr int kReaders = 4;
    constexpr int kIterations = 200;

    std::vector<std::thread> threads;
    for (int p = 0; p < kPublishers; ++p) {
        threads.emplace_back([&store, p] {
            for (int i = 0; i < kIterations; ++i) {
                auto snap = makeSnapshotWithOperations(10);
                snap.operations[0].operationId =
                    "pub" + std::to_string(p) + "-" + std::to_string(i);
                store.publish(snap);
            }
        });
    }
    for (int r = 0; r < kReaders; ++r) {
        threads.emplace_back([&store] {
            for (int i = 0; i < kIterations; ++i) {
                auto snap = store.snapshot();
                (void)snap.operations.size();
                (void)store.findOperation("any");
            }
        });
    }
    for (auto& t : threads) t.join();

    // 数据竞争/撕裂不会被本测试直接检测，但至少保证并发发布+读取不崩溃、尺寸正确。
    EXPECT_EQ(store.snapshot().operations.size(), 10u);
}

}  // namespace
}  // namespace application_vnext::control
