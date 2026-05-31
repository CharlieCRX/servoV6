// =============================================================================
// TDD 阶段 5 Sprint 1: pollFeedback 可信度门禁测试
//
// 测试用例 6: 不可信快照不注入反馈
//   当 PlcDevice::isStateTrusted() 返回 false 时，
//   ModbusSystemDriver::pollFeedback() 应直接返回，
//   不对 SystemContext 中的任何轴注入反馈数据。
//
// 设计依据:
//   《阶段五：分层开发路线图：8 个可控步骤，逐步推进》 Sprint 1
//   1 行改动（PlcDevice::isStateTrusted() 加 virtual）+ 1 个测试用例
// =============================================================================

#include <gtest/gtest.h>
#include <memory>
#include "infrastructure/plc/ModbusSystemDriver.h"
#include "infrastructure/plc/protocol/PlcDevice.h"
#include "infrastructure/plc/protocol/ProtocolProfile.h"
#include "domain/entity/SystemContext.h"

using namespace plc;

namespace {

/// @brief 手动 Mock PlcDevice: isStateTrusted() 始终返回 false
///
/// 模拟通讯层快照不可信场景（如连接断开、快照过期等），
/// 用于验证 pollFeedback 门禁在不可信状态下正确拦截反馈注入。
class UntrustedPlcDevice : public protocol::PlcDevice {
public:
    using protocol::PlcDevice::PlcDevice;
    bool isStateTrusted() const override { return false; }
};

} // namespace

// =============================================================================
// 测试夹具
// =============================================================================
class ModbusSystemDriverPollFeedbackTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 使用 nullptr 作为 IModbusClient（isStateTrusted 不涉及通讯）
        m_mockDevice = std::make_shared<UntrustedPlcDevice>(
            protocol::INOVANCE_PROFILE
        );
        m_driver.setDevice(m_mockDevice.get());
    }

    /// @brief 通过 tryReadAxis 获取指定轴指针（绕过龙门语义拦截用于测试）
    Axis* getAxis(SystemContext& ctx, AxisId id) {
        Axis* axis = nullptr;
        ContextRejection reason;
        ctx.tryReadAxis(id, axis, reason);
        return axis;
    }

    ModbusSystemDriver m_driver;
    SystemContext m_ctx;
    std::shared_ptr<UntrustedPlcDevice> m_mockDevice;
};

// =============================================================================
// 测试用例 6: 不可信快照不注入反馈
//
// Given:  PlcDevice::isStateTrusted() 返回 false（快照不可信）
// When:   调用 ModbusSystemDriver::pollFeedback(ctx)
// Then:   所有轴的 state 保持构造默认值 AxisState::Unknown
//         所有轴的绝对位置保持构造默认值 0.0
//         pollFeedback 不抛异常，不崩溃
// =============================================================================
TEST_F(ModbusSystemDriverPollFeedbackTest, ShouldNotInjectFeedbackWhenSnapshotUntrusted) {
    // Act: 调用 pollFeedback
    //      m_device->isStateTrusted() 返回 false → 门禁生效 → 直接 return
    EXPECT_NO_THROW(m_driver.pollFeedback(m_ctx));

    // Assert: 所有轴的 state 仍为构造默认值 AxisState::Unknown
    //         X/X1/X2 轴受龙门语义拦截（NotSynchronized），与 pollFeedback 无关
    for (auto id : {AxisId::Y, AxisId::Z, AxisId::R}) {
        Axis* axis = getAxis(m_ctx, id);
        ASSERT_NE(axis, nullptr) << "failed to get axis: " << static_cast<int>(id);
        EXPECT_EQ(axis->state(), AxisState::Unknown)
            << "axis " << static_cast<int>(id) << " should remain Unknown";
        EXPECT_DOUBLE_EQ(axis->currentAbsolutePosition(), 0.0)
            << "axis " << static_cast<int>(id) << " position should remain 0.0";
    }
}

// =============================================================================
// 测试用例 6b: 未绑定 PlcDevice 时 pollFeedback 不崩溃
//
// Given:  ModbusSystemDriver 未绑定 PlcDevice（m_device == nullptr）
// When:   调用 ModbusSystemDriver::pollFeedback(ctx)
// Then:   不抛异常，不崩溃（空操作）
// =============================================================================
TEST_F(ModbusSystemDriverPollFeedbackTest, ShouldNotCrashWhenDeviceNotBound) {
    // Given: 构造未绑定设备的 driver
    ModbusSystemDriver unboundDriver;
    SystemContext ctx;

    // When & Then: pollFeedback 不崩溃
    EXPECT_NO_THROW(unboundDriver.pollFeedback(ctx));
}
