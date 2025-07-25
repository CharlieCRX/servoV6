// tests/test_businesslogic.cpp
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "BusinessLogic.h"
#include "MovementCommand.h"
#include "mocks/MockMotor.h" // 确保路径正确
#include <Logger.h>

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;

class BusinessLogicTest : public ::testing::Test {
protected:
    MockMotor* mockMotorRawPtr; // 裸指针，用于设置期望
    std::unique_ptr<BusinessLogic> businessLogic; // 业务逻辑实例

    void SetUp() override {
        auto mockMotorUniquePtr = std::make_unique<MockMotor>();
        mockMotorRawPtr = mockMotorUniquePtr.get();

        std::map<std::string, std::unique_ptr<IMotor>> motors;
        motors["main_motor"] = std::move(mockMotorUniquePtr);

        businessLogic = std::make_unique<BusinessLogic>(std::move(motors));
    }

    void TearDown() override {
        // unique_ptr 会自动管理内存
    }
};

TEST_F(BusinessLogicTest, ExecutesComplexMovementSequence) {
    // 预期行为：设置速度 -> 相对移动 -> 停顿 -> 回原点
    InSequence s;

    // 1. 期望 setSpeed(20.0) 被调用一次，并返回 true
    EXPECT_CALL(*mockMotorRawPtr, setSpeed(20.0)).WillOnce(Return(true));

    // 2. 期望 relativeMove(30.0) 被调用一次，并返回 true
    EXPECT_CALL(*mockMotorRawPtr, relativeMove(30.0)).WillOnce(Return(true));

    // 3. 期望 wait(2000) 被调用一次
    EXPECT_CALL(*mockMotorRawPtr, wait(2000));

    // 4. 期望 goHome() 被调用一次，并返回 true
    EXPECT_CALL(*mockMotorRawPtr, goHome()).WillOnce(Return(true));

    // 准备命令序列
    CommandSequence commands;
    commands.push_back(SetSpeed{20.0});       // 设置速度 20mm/s
    commands.push_back(RelativeMove{30.0});   // 相对移动 30mm
    commands.push_back(Wait{2000});           // 停顿 2000ms
    commands.push_back(GoHome{});             // 回原点

    // 调用被测试的业务逻辑方法
    bool result = businessLogic->executeCommandSequence("main_motor", commands);

    // 断言整个命令序列执行成功
    ASSERT_TRUE(result);
}

// 考虑添加一个测试，测试当某个电机操作失败时 BusinessLogic 的行为
TEST_F(BusinessLogicTest, ReturnsFalseOnMotorOperationFailure) {
    InSequence s;

    EXPECT_CALL(*mockMotorRawPtr, setSpeed(20.0)).WillOnce(Return(true));
    // 模拟 relativeMove 失败
    EXPECT_CALL(*mockMotorRawPtr, relativeMove(30.0)).WillOnce(Return(false));
    // 期望后续的 wait 和 goHome 不会被调用
    EXPECT_CALL(*mockMotorRawPtr, wait(_)).Times(0);
    EXPECT_CALL(*mockMotorRawPtr, goHome()).Times(0);

    CommandSequence commands;
    commands.push_back(SetSpeed{20.0});
    commands.push_back(RelativeMove{30.0});
    commands.push_back(Wait{2000});
    commands.push_back(GoHome{});

    bool result = businessLogic->executeCommandSequence("main_motor", commands);

    // 断言执行失败
    ASSERT_FALSE(result);
}
