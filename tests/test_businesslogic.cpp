// tests/test_businesslogic.cpp
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "BusinessLogic.h"
#include "MovementCommand.h"
#include "mocks/MockMotor.h" // 确保路径正确

// 包含 Logger 头文件，现在 Logger.h 位于 utils/ 目录下
// 由于 CMake 已经正确配置了 utilslib 的包含路径，这里可以直接 <Logger.h>

// 在测试用例的 SetUp 阶段使用日志
// 注意：Logger::init() 应该在 test_main.cpp 中全局初始化一次
// 所以这里直接使用日志宏即可。
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
    commands.push_back(SetSpeed{20.0});        // 设置速度 20mm/s
    commands.push_back(RelativeMove{30.0});    // 相对移动 30mm
    commands.push_back(Wait{2000});            // 停顿 2000ms
    commands.push_back(GoHome{});              // 回原点

    // 调用被测试的业务逻辑方法
    bool result = businessLogic->executeCommandSequence("main_motor", commands);

    // 断言整个命令序列执行成功
    ASSERT_TRUE(result);
}

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

TEST_F(BusinessLogicTest, ExecutesAbsoluteMovement) {
    // 预期行为：设置速度 -> 绝对移动 -> 回原点
    InSequence s;

    // 1. 期望 setSpeed(10.0) 被调用一次，并返回 true
    EXPECT_CALL(*mockMotorRawPtr, setSpeed(10.0)).WillOnce(Return(true));

    // 2. 期望 absoluteMove(50.0) 被调用一次，并返回 true
    // 在 BusinessLogic 中尚未处理 AbsoluteMove 时，这个 EXPECT_CALL 将不会被触发
    // 或者，如果 BusinessLogic::executeCommandSequence 遇到未处理的 variant 变体，它可能会崩溃或返回 false
    EXPECT_CALL(*mockMotorRawPtr, absoluteMove(50.0)).WillOnce(Return(true));

    // 3. 期望 goHome() 被调用一次，并返回 true
    EXPECT_CALL(*mockMotorRawPtr, goHome()).WillOnce(Return(true));

    // 准备命令序列，包含新的 AbsoluteMove 命令
    CommandSequence commands;
    commands.push_back(SetSpeed{10.0});
    commands.push_back(AbsoluteMove{50.0}); // 移动到绝对位置 50mm
    commands.push_back(GoHome{});

    // 调用被测试的业务逻辑方法
    bool result = businessLogic->executeCommandSequence("main_motor", commands);

    // 断言整个命令序列执行成功
    ASSERT_TRUE(result);
    LOG_INFO("Test 'ExecutesAbsoluteMovement' finished successfully.");
}
