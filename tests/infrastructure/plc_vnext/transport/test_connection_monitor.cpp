// ============================================================================
// test_connection_monitor.cpp —— Step 5 transport: 连接监控
// ============================================================================
// 验证 ConnectionMonitor：
//   - 断线 → 连接状态更新；
//   - requestReconnect() 委托底层客户端；
//   - 监控层绝不重放任何命令（断连/重连全程零写）。
// ============================================================================
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/fake/FakeModbusClient.h"
#include "infrastructure/plc_vnext/transport/ConnectionMonitor.h"

namespace plc_vnext {
namespace {

using contracts::CommunicationResult;
using fake::FakeModbusClient;
using transport::ConnectionMonitor;

// ───────────────────────────────────────────────
// 断线 → 连接状态更新
// ───────────────────────────────────────────────
TEST(ConnectionMonitor, Disconnect_ReportsState) {
    auto fake = std::make_shared<FakeModbusClient>();
    ConnectionMonitor monitor(fake);

    // 初始：底层默认已连接。
    EXPECT_TRUE(monitor.isConnected());

    // 注入一次断连结果。
    monitor.onTransactionResult(
        CommunicationResult::disconnected("link lost"));

    EXPECT_FALSE(monitor.isConnected());
    EXPECT_EQ(monitor.consecutiveFailures(), 1u);
    EXPECT_TRUE(monitor.isBackoffActive());
    // 首次失败即进入退避，退避应非零。
    EXPECT_GT(monitor.backoffMs(), 0u);
}

// ───────────────────────────────────────────────
// 手动重连 → 委托底层客户端
// ───────────────────────────────────────────────
TEST(ConnectionMonitor, ManualReconnect_Delegates) {
    auto fake = std::make_shared<FakeModbusClient>();
    fake->setConnected(false);

    ConnectionMonitor monitor(fake);
    EXPECT_FALSE(monitor.isConnected());

    // requestReconnect 必须委托给底层客户端（fake 记录重连次数）。
    monitor.requestReconnect();
    EXPECT_EQ(fake->requestReconnectCount(), 1u);

    // 与底层同步后应反映为已连接。
    monitor.refresh();
    EXPECT_TRUE(monitor.isConnected());
}

// ───────────────────────────────────────────────
// 监控层绝不重放运动命令（断连/重连全程零写）
// ───────────────────────────────────────────────
TEST(ConnectionMonitor, NeverReplaysMotionCommand) {
    auto fake = std::make_shared<FakeModbusClient>();
    ConnectionMonitor monitor(fake);

    // 走一遍断连 → 观察 → 重连 → 同步的生命周期。
    monitor.onTransactionResult(
        CommunicationResult::disconnected("link lost"));
    monitor.onTransactionResult(CommunicationResult::sent());
    monitor.requestReconnect();
    monitor.refresh();

    // ConnectionMonitor 只观察/委托，绝不该向设备发出任何写命令。
    EXPECT_TRUE(fake->writtenCoils().empty());
    EXPECT_TRUE(fake->writtenRegisters().empty());
    EXPECT_TRUE(fake->writtenMulti().empty());
}

// ───────────────────────────────────────────────
// 指数退避：500→1000→2000→4000→8000，之后封顶
// ───────────────────────────────────────────────
TEST(ConnectionMonitor, Backoff_DoublesUpToCap) {
    auto fake = std::make_shared<FakeModbusClient>();
    ConnectionMonitor monitor(fake);

    // 用非连接性失败（Timeout）驱动退避递增，避免误改 connected 语义。
    const CommunicationResult fail{CommunicationResult::Status::Timeout, 0, "t"};

    monitor.onTransactionResult(fail);
    EXPECT_EQ(monitor.backoffMs(), 500u);
    monitor.onTransactionResult(fail);
    EXPECT_EQ(monitor.backoffMs(), 1000u);
    monitor.onTransactionResult(fail);
    EXPECT_EQ(monitor.backoffMs(), 2000u);
    monitor.onTransactionResult(fail);
    EXPECT_EQ(monitor.backoffMs(), 4000u);
    monitor.onTransactionResult(fail);
    EXPECT_EQ(monitor.backoffMs(), 8000u);
    monitor.onTransactionResult(fail);
    EXPECT_EQ(monitor.backoffMs(), 8000u);  // 封顶，不再增长
}

// ───────────────────────────────────────────────
// 成功后重置失败计数与退避，并恢复已连接
// ───────────────────────────────────────────────
TEST(ConnectionMonitor, Success_ResetsBackoff) {
    auto fake = std::make_shared<FakeModbusClient>();
    ConnectionMonitor monitor(fake);

    const CommunicationResult fail{CommunicationResult::Status::NetworkError,
                                   0, "net"};
    monitor.onTransactionResult(fail);
    monitor.onTransactionResult(fail);
    EXPECT_EQ(monitor.consecutiveFailures(), 2u);
    EXPECT_FALSE(monitor.isConnected());
    EXPECT_TRUE(monitor.isBackoffActive());

    monitor.onTransactionResult(CommunicationResult::sent());
    EXPECT_TRUE(monitor.isConnected());
    EXPECT_EQ(monitor.consecutiveFailures(), 0u);
    EXPECT_EQ(monitor.backoffMs(), 0u);
    EXPECT_FALSE(monitor.isBackoffActive());
}

}  // namespace
}  // namespace plc_vnext
