#include <gtest/gtest.h>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include "presentation/viewmodel/QtAxisViewModel.h"
#include "presentation/viewmodel/AxisViewModelCore.h"
#include "presentation/viewmodel/ViewModelError.h"
#include "application/SystemManager.h"
#include "domain/entity/SystemContext.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"

// ============================================================================
// QtAxisViewModel 单元测试
//
// 测试目标：
//   1. 验证 QtAxisViewModel 正确透传 Core 的属性
//   2. 验证新增 Q_PROPERTY（isEnabled, stateText, errorCategory, errorCount）
//   3. 验证 Q_INVOKABLE 控制方法正确转发
//   4. 验证错误管理接口（getAllErrors, acknowledgeError, acknowledgeAllErrors）
//   5. 验证节流信号机制（tick 按需 emit）
// ============================================================================

class QtAxisViewModelTest : public ::testing::Test {
protected:
    static constexpr const char* GROUP_NAME = "QtMachine";

    FakePLC plc;
    FakeAxisDriver driver{plc};
    SystemManager manager;
    SystemContext* ctx = nullptr;

    std::unique_ptr<AxisViewModelCore> core;
    std::unique_ptr<QtAxisViewModel> qvm;

    const int TICK_MS = 10;

    void SetUp() override {
        ContextRejection reason;
        ASSERT_TRUE(manager.createGroup(GROUP_NAME, reason));
        ASSERT_TRUE(manager.tryGetGroup(GROUP_NAME, ctx, reason));
        ASSERT_NE(ctx, nullptr);
        ctx->setDriver(&driver);

        plc.forceState(AxisId::Y, AxisState::Disabled);
        plc.setSimulatedMoveVelocity(AxisId::Y, 25.0);
        plc.setSimulatedJogVelocity(AxisId::Y, 20.0);
        plc.setLimits(AxisId::Y, 1000.0, -1000.0);

        driver.pollFeedback(*ctx);

        core = std::make_unique<AxisViewModelCore>(manager, GROUP_NAME, AxisId::Y);
        core->setJogVelocity(20.0);
        core->setMoveVelocity(25.0);

        qvm = std::make_unique<QtAxisViewModel>(core.get(), nullptr);
    }

    void advanceTime(int totalMs) {
        int elapsed = 0;
        while (elapsed < totalMs) {
            qvm->tick();
            driver.pollFeedback(*ctx);
            elapsed += TICK_MS;
        }
    }

    bool waitUntil(std::function<bool()> condition, int timeoutMs = 5000) {
        int elapsed = 0;
        while (elapsed < timeoutMs) {
            if (condition()) return true;
            advanceTime(TICK_MS);
            elapsed += TICK_MS;
        }
        return false;
    }
};

// =========================================================
// 测试 Q1：state() 透传
// =========================================================
TEST_F(QtAxisViewModelTest, ShouldReflectCoreState) {
    EXPECT_EQ(qvm->state(), static_cast<int>(AxisState::Disabled));

    core->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::Idle; }));
    qvm->tick();
    EXPECT_EQ(qvm->state(), static_cast<int>(AxisState::Idle));
}

// =========================================================
// 测试 Q2：absPos / relPos 透传
// =========================================================
TEST_F(QtAxisViewModelTest, ShouldReflectCorePosition) {
    EXPECT_DOUBLE_EQ(qvm->absPos(), 0.0);
    EXPECT_DOUBLE_EQ(qvm->relPos(), 0.0);

    core->moveAbsolute(100.0);
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::Disabled; }, 10000));

    qvm->tick();
    EXPECT_NEAR(qvm->absPos(), 100.0, 0.01);
}

// =========================================================
// 测试 Q3：isEnabled 属性
// =========================================================
TEST_F(QtAxisViewModelTest, ShouldReportIsEnabled) {
    EXPECT_FALSE(qvm->isEnabled());

    core->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::Idle; }));
    qvm->tick();
    EXPECT_TRUE(qvm->isEnabled());

    core->disable();
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::Disabled; }));
    qvm->tick();
    EXPECT_FALSE(qvm->isEnabled());
}

// =========================================================
// 测试 Q4：stateText 属性
// =========================================================
TEST_F(QtAxisViewModelTest, ShouldReturnCorrectStateText) {
    qvm->tick();
    EXPECT_EQ(qvm->stateText().toStdString(), "Disabled");

    core->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::Idle; }));
    qvm->tick();
    EXPECT_EQ(qvm->stateText().toStdString(), "Standstill");
}

// =========================================================
// 测试 Q5：errorCategory 属性
// =========================================================
TEST_F(QtAxisViewModelTest, ShouldReturnErrorCategoryNoneWhenNoError) {
    EXPECT_FALSE(qvm->hasError());
    EXPECT_EQ(qvm->errorCategory().toStdString(), "None");
}

// =========================================================
// 测试 Q6：errorCount 属性
// =========================================================
TEST_F(QtAxisViewModelTest, ShouldReportErrorCount) {
    EXPECT_EQ(qvm->errorCount(), 0);

    core->zeroAbsolutePosition();  // 在 Disabled 状态产生错误
    qvm->tick();
    EXPECT_GT(qvm->errorCount(), 0);
}

// =========================================================
// 测试 Q7：hasError / errorCode / errorMessage 透传
// =========================================================
TEST_F(QtAxisViewModelTest, ShouldReflectCoreErrorProperties) {
    EXPECT_FALSE(qvm->hasError());
    EXPECT_TRUE(qvm->errorCode().isEmpty());
    EXPECT_TRUE(qvm->errorMessage().isEmpty());

    core->zeroAbsolutePosition();
    qvm->tick();

    EXPECT_TRUE(qvm->hasError());
    EXPECT_EQ(qvm->errorCode().toStdString(), "CTX_AXIS_NOT_REGISTERED");
    EXPECT_FALSE(qvm->errorMessage().isEmpty());
}

// =========================================================
// 测试 Q8：getAllErrors() 返回完整错误列表
// =========================================================
TEST_F(QtAxisViewModelTest, GetAllErrorsShouldReturnList) {
    EXPECT_TRUE(qvm->getAllErrors().isEmpty());

    core->zeroAbsolutePosition();
    core->setRelativeZero();
    qvm->tick();

    QVariantList errors = qvm->getAllErrors();
    EXPECT_GE(errors.size(), 2);
    EXPECT_TRUE(errors[0].toMap()["code"].toString().startsWith("CTX_"));
}

// =========================================================
// 测试 Q9：acknowledgeError 单条确认
// =========================================================
TEST_F(QtAxisViewModelTest, AcknowledgeErrorShouldRemoveOne) {
    core->zeroAbsolutePosition();
    qvm->tick();
    ASSERT_GT(qvm->errorCount(), 0);

    int before = qvm->errorCount();
    qvm->acknowledgeError(0);
    qvm->tick();
    EXPECT_EQ(qvm->errorCount(), before - 1);
}

// =========================================================
// 测试 Q10：acknowledgeAllErrors 全部清除
// =========================================================
TEST_F(QtAxisViewModelTest, AcknowledgeAllErrorsShouldClearAll) {
    core->zeroAbsolutePosition();
    core->setRelativeZero();
    qvm->tick();
    ASSERT_GE(qvm->errorCount(), 2);

    qvm->acknowledgeAllErrors();
    qvm->tick();
    EXPECT_EQ(qvm->errorCount(), 0);
    EXPECT_FALSE(qvm->hasError());
}

// =========================================================
// 测试 Q11：clearError() 兼容清除
// =========================================================
TEST_F(QtAxisViewModelTest, ClearErrorShouldWork) {
    core->zeroAbsolutePosition();
    qvm->tick();
    ASSERT_TRUE(qvm->hasError());

    qvm->clearError();
    qvm->tick();
    EXPECT_FALSE(qvm->hasError());
    EXPECT_TRUE(qvm->getAllErrors().isEmpty());
}

// =========================================================
// 测试 Q12：Jog 控制方法转发
// =========================================================
TEST_F(QtAxisViewModelTest, JogMethodsShouldForwardToCore) {
    core->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::Idle; }));

    double before = core->absPos();
    qvm->jogPositivePressed();
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::Jogging; }));

    advanceTime(300);
    EXPECT_GT(core->absPos(), before + 2.0);

    qvm->jogPositiveReleased();
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::Disabled; }));
}

// =========================================================
// 测试 Q13：moveAbsolute / moveRelative 转发
// =========================================================
TEST_F(QtAxisViewModelTest, MoveAbsoluteShouldForwardToCore) {
    qvm->moveAbsolute(50.0);
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::Disabled; }, 10000));
    EXPECT_NEAR(core->absPos(), 50.0, 0.01);
}

// =========================================================
// 测试 Q14：零位操作转发
// =========================================================
TEST_F(QtAxisViewModelTest, ZeroOperationsShouldForwardToCore) {
    core->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::Idle; }));

    core->moveAbsolute(50.0);
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::Disabled; }, 10000));

    core->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::Idle; }));

    qvm->setRelativeZero();
    qvm->tick();
    ASSERT_TRUE(waitUntil([this]() { return std::abs(core->relPos()) < 0.01; }, 5000));
    EXPECT_NEAR(core->relPos(), 0.0, 0.01);
}

// =========================================================
// 测试 Q15：stop() 转发
// =========================================================
TEST_F(QtAxisViewModelTest, StopShouldForwardToCore) {
    core->moveAbsolute(800.0);
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::MovingAbsolute; }));

    advanceTime(500);
    double midPos = core->absPos();

    qvm->stop();
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::Disabled; }));

    EXPECT_LT(core->absPos(), 700.0);
}

// =========================================================
// 测试 Q16：速度设置转发
// =========================================================
TEST_F(QtAxisViewModelTest, SetVelocityShouldForwardToCore) {
    qvm->setJogVelocity(100.0);
    qvm->tick();
    EXPECT_DOUBLE_EQ(core->jogVelocity(), 100.0);

    qvm->setMoveVelocity(120.0);
    qvm->tick();
    EXPECT_DOUBLE_EQ(core->moveVelocity(), 120.0);
}

// =========================================================
// 测试 Q17：状态变化时 tick 驱动属性更新
// =========================================================
TEST_F(QtAxisViewModelTest, TickShouldDriveStatePropertyUpdate) {
    EXPECT_EQ(qvm->state(), static_cast<int>(AxisState::Disabled));

    core->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return core->state() == AxisState::Idle; }));
    qvm->tick();

    EXPECT_EQ(qvm->state(), static_cast<int>(AxisState::Idle));
    EXPECT_TRUE(qvm->isEnabled());
    EXPECT_EQ(qvm->stateText().toStdString(), "Standstill");
}

// =========================================================
// 测试 Q18：tick 驱动错误计数属性更新
// =========================================================
TEST_F(QtAxisViewModelTest, TickShouldDriveErrorCountPropertyUpdate) {
    EXPECT_EQ(qvm->errorCount(), 0);

    core->zeroAbsolutePosition();
    qvm->tick();

    EXPECT_GT(qvm->errorCount(), 0);
    EXPECT_TRUE(qvm->hasError());
}

// =========================================================
// 测试 Q19：posLimit / negLimit 透传
// =========================================================
TEST_F(QtAxisViewModelTest, ShouldReflectLimits) {
    EXPECT_DOUBLE_EQ(qvm->posLimit(), 1000.0);
    EXPECT_DOUBLE_EQ(qvm->negLimit(), -1000.0);
}

// =========================================================
// 测试 Q20：Core 为空时的安全边界
// =========================================================
TEST_F(QtAxisViewModelTest, ShouldHandleNullCoreGracefully) {
    QtAxisViewModel nullQvm(nullptr, nullptr);

    EXPECT_EQ(nullQvm.state(), 0);
    EXPECT_DOUBLE_EQ(nullQvm.absPos(), 0.0);
    EXPECT_DOUBLE_EQ(nullQvm.relPos(), 0.0);
    EXPECT_FALSE(nullQvm.isEnabled());
    EXPECT_EQ(nullQvm.stateText().toStdString(), "Unavailable");
    EXPECT_EQ(nullQvm.errorCategory().toStdString(), "None");
    EXPECT_EQ(nullQvm.errorCount(), 0);
    EXPECT_FALSE(nullQvm.hasError());
    EXPECT_TRUE(nullQvm.getAllErrors().isEmpty());

    // 方法调用不应崩溃
    nullQvm.jogPositivePressed();
    nullQvm.stop();
    nullQvm.clearError();
    nullQvm.tick();
}
