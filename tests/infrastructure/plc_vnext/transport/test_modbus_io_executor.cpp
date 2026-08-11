// ============================================================================
// test_modbus_io_executor.cpp —— Step 5 transport: 单通道串行化 I/O
// ============================================================================
// 验证 ModbusIoExecutor 把并发事务串行到单通道：
//   - 并发提交互不交叉（每个事务读回自己的地址数据）；
//   - 失败请求不吞掉后续响应，且保序返回。
// ============================================================================
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/fake/FakeModbusClient.h"
#include "infrastructure/plc_vnext/transport/IModbusClient.h"
#include "infrastructure/plc_vnext/transport/ModbusIoExecutor.h"

namespace plc_vnext {
namespace {

using contracts::CommunicationResult;
using fake::FakeModbusClient;
using transport::IModbusClient;
using transport::ModbusIoExecutor;

// ------------------------------------------------------------------
// 记录"同时 in-flight 最大数"的阻塞 stub。
// 它自身不加锁、带真实延迟，专门用来证明 executor 是否真正串行化：
//   - 若 executor 不串行，多个线程会同时进入 readHoldingRegisters，
//     maxInflight() 将 > 1；
//   - 若 executor 串行（同一时刻只放行一笔整事务），maxInflight() == 1。
// ------------------------------------------------------------------
class TrackingClient : public IModbusClient {
public:
    bool isConnected() const override { return true; }
    void requestReconnect() override {}

    CommunicationResult readCoils(uint16_t, uint16_t,
                                  std::vector<uint8_t>&) override {
        return CommunicationResult::sent();
    }

    CommunicationResult readHoldingRegisters(
        uint16_t startAddress, uint16_t count,
        std::vector<uint16_t>& payload) override {
        // 进入：登记当前 in-flight，并刷新历史最大值。
        const unsigned cur = m_inflight.fetch_add(1) + 1;
        unsigned observed = m_max.load(std::memory_order_relaxed);
        while (cur > observed &&
               !m_max.compare_exchange_weak(observed, cur,
                                            std::memory_order_relaxed)) {
        }

        // 放大串行化窗口：若 executor 未加锁，这段延迟内其它线程会并发进入。
        std::this_thread::sleep_for(std::chrono::microseconds(200));

        payload.resize(count);
        for (uint16_t i = 0; i < count; ++i) payload[i] = startAddress + i;

        m_inflight.fetch_sub(1);
        return CommunicationResult::sent();
    }

    CommunicationResult writeSingleCoil(uint16_t, bool) override {
        return CommunicationResult::sent();
    }
    CommunicationResult writeSingleRegister(uint16_t, uint16_t) override {
        return CommunicationResult::sent();
    }
    CommunicationResult writeMultipleRegisters(
        uint16_t, const std::vector<uint16_t>&) override {
        return CommunicationResult::sent();
    }

    unsigned maxInflight() const {
        return m_max.load(std::memory_order_relaxed);
    }

private:
    std::atomic<unsigned> m_inflight{0};
    std::atomic<unsigned> m_max{0};
};

// ───────────────────────────────────────────────
// 并发提交 → 单通道串行、最大 in-flight 严格为 1
// ───────────────────────────────────────────────
TEST(ModbusIoExecutor, SerializesRequests_MaxInflightIsOne) {
    auto client = std::make_shared<TrackingClient>();
    ModbusIoExecutor executor(client);

    constexpr int kThreads = 8;
    constexpr int kIters = 200;
    std::atomic<bool> mismatch{false};

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        const uint16_t base = static_cast<uint16_t>(t * 100);
        workers.emplace_back([&executor, base, &mismatch]() {
            for (int i = 0; i < kIters; ++i) {
                std::vector<uint16_t> payload;
                auto res = executor.readHoldingRegisters(base, 8, payload);
                if (!res.ok() || payload.size() != 8) {
                    mismatch.store(true);
                    return;
                }
                for (uint16_t j = 0; j < 8; ++j) {
                    if (payload[j] != base + j) {
                        mismatch.store(true);
                        return;
                    }
                }
            }
        });
    }
    for (auto& w : workers) w.join();

    // 事务必须被串行化：同一时刻只有一笔在底层执行，绝不交叉。
    EXPECT_EQ(client->maxInflight(), 1u)
        << "executor 未真正串行化：观察到同时 in-flight 的底层事务 > 1";
    EXPECT_FALSE(mismatch.load())
        << "并发读事务发生交叉：某个线程读回了别的线程的地址数据";
}

// ───────────────────────────────────────────────
// 失败请求不吞掉后续响应，且保序返回
// ───────────────────────────────────────────────
TEST(ModbusIoExecutor, Failure_PreservesResponseOrder) {
    auto fake = std::make_shared<FakeModbusClient>();
    fake->setHoldingRegister(10, 0xAA);
    fake->setHoldingRegister(20, 0xBB);
    // 地址 >= 100 的操作全部失败（模拟某段寄存器通讯故障）。
    fake->setFailureThreshold(100);

    ModbusIoExecutor executor(fake);

    std::vector<uint16_t> p1, p2, p3;
    auto r1 = executor.readHoldingRegisters(10, 1, p1);   // 命中
    auto r2 = executor.readHoldingRegisters(200, 1, p2);  // 失败
    auto r3 = executor.readHoldingRegisters(20, 1, p3);   // 命中，且不应被吞掉

    // 顺序 = 提交顺序：成功 → 失败 → 成功。
    EXPECT_TRUE(r1.ok());
    ASSERT_EQ(p1.size(), 1u);
    EXPECT_EQ(p1[0], 0xAAu);

    EXPECT_FALSE(r2.ok());
    EXPECT_EQ(r2.status, CommunicationResult::Status::NetworkError);
    EXPECT_TRUE(p2.empty());  // 失败不提供伪造数据

    EXPECT_TRUE(r3.ok());
    ASSERT_EQ(p3.size(), 1u);
    EXPECT_EQ(p3[0], 0xBBu);
}

}  // namespace
}  // namespace plc_vnext
