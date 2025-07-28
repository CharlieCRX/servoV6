// tests/test_businesslogic.cpp (新的测试夹具和用例)
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "BusinessLogic.h"
#include "MovementCommand.h"
#include "mocks/MockServoAdapters.h" // 引入 Mock 适配器
#include "Logger.h" // 假设你有日志

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;

// Test fixture for linear motor scenarios
class LinearServoBusinessLogicTest : public ::testing::Test {
protected:
    MockLinearServoAdapter* mockLinearAdapterRawPtr;
    std::unique_ptr<BusinessLogic> businessLogic;

    void SetUp() override {
        auto mockLinearAdapterUniquePtr = std::make_unique<MockLinearServoAdapter>();
        mockLinearAdapterRawPtr = mockLinearAdapterUniquePtr.get();

        std::map<std::string, std::unique_ptr<IServoAdapter>> adapters;
        adapters["linear_motor"] = std::move(mockLinearAdapterUniquePtr);
        businessLogic = std::make_unique<BusinessLogic>(std::move(adapters));
    }
};

// Test fixture for rotary motor scenarios
class RotaryServoBusinessLogicTest : public ::testing::Test {
protected:
    MockRotaryServoAdapter* mockRotaryAdapterRawPtr;
    std::unique_ptr<BusinessLogic> businessLogic;

    void SetUp() override {
        auto mockRotaryAdapterUniquePtr = std::make_unique<MockRotaryServoAdapter>();
        mockRotaryAdapterRawPtr = mockRotaryAdapterUniquePtr.get();

        std::map<std::string, std::unique_ptr<IServoAdapter>> adapters;
        adapters["rotary_motor"] = std::move(mockRotaryAdapterUniquePtr);
        businessLogic = std::make_unique<BusinessLogic>(std::move(adapters));
    }
};

// Test for Linear Motor: Executes a complex linear sequence
TEST_F(LinearServoBusinessLogicTest, ExecutesComplexLinearMovementSequence) {
    InSequence s;
    EXPECT_CALL(*mockLinearAdapterRawPtr, setPositionSpeed(20.0)).WillOnce(Return(true));
    EXPECT_CALL(*mockLinearAdapterRawPtr, relativeMove(30.0)).WillOnce(Return(true));
    EXPECT_CALL(*mockLinearAdapterRawPtr, wait(2000));
    EXPECT_CALL(*mockLinearAdapterRawPtr, goHome()).WillOnce(Return(true));

    CommandSequence commands;
    commands.push_back(SetPositionSpeed{20.0});
    commands.push_back(RelativeMove{30.0});
    commands.push_back(Wait{2000});
    commands.push_back(GoHome{});

    ASSERT_TRUE(businessLogic->executeCommandSequence("linear_motor", commands));
}

// Test for Rotary Motor: Executes angular movement
TEST_F(RotaryServoBusinessLogicTest, ExecutesAngularMovementSequence) {
    InSequence s;
    EXPECT_CALL(*mockRotaryAdapterRawPtr, setAngularPositionSpeed(60.0)).WillOnce(Return(true));
    EXPECT_CALL(*mockRotaryAdapterRawPtr, angularMove(90.0)).WillOnce(Return(true));
    EXPECT_CALL(*mockRotaryAdapterRawPtr, goHome()).WillOnce(Return(true));

    CommandSequence commands;
    commands.push_back(SetAngularPositionSpeed{60.0});
    commands.push_back(AngularMove{90.0});
    commands.push_back(GoHome{});

    ASSERT_TRUE(businessLogic->executeCommandSequence("rotary_motor", commands));
}

// Test: Linear command on Rotary Adapter (should fail)
TEST_F(RotaryServoBusinessLogicTest, LinearCommandOnRotaryAdapterFails) {
    CommandSequence commands;
    commands.push_back(SetPositionSpeed{10.0}); // Linear command

    ASSERT_FALSE(businessLogic->executeCommandSequence("rotary_motor", commands));
}

// Test: Angular command on Linear Adapter (should fail)
TEST_F(LinearServoBusinessLogicTest, AngularCommandOnLinearAdapterFails) {
    CommandSequence commands;
    commands.push_back(SetAngularPositionSpeed{10.0}); // Angular command

    ASSERT_FALSE(businessLogic->executeCommandSequence("linear_motor", commands));
}

// 当然，你需要为所有之前通过的测试用例创建相应的 LinearServoBusinessLogicTest 或 RotaryServoBusinessLogicTest 版本。
// 并且，你需要为 LinearServoAdapter 和 RotaryServoAdapter 本身编写独立的单元测试，验证它们能否正确地将 mm/degrees 转换为 RPM/revolutions。
