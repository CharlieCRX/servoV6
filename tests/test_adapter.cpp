// tests/test_adapters.cpp (新建文件)
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "LinearServoAdapter.h" // 新的头文件
#include "RotaryServoAdapter.h" // 新的头文件
#include "MockMotor.h" // Mock IMotor
#include "Logger.h" // 假设需要 Logger

using ::testing::_;
using ::testing::Return;
using ::testing::NiceMock; // 使用 NiceMock 避免未预期的调用警告

// --- LinearServoAdapter Tests ---
class LinearServoAdapterTest : public ::testing::Test {
protected:
    // 使用 std::unique_ptr 来管理 MockMotor 的生命周期
    // 并且用 NiceMock 包装，避免不关心的调用产生警告
    std::unique_ptr<NiceMock<MockMotor>> mockMotorUniquePtr;
    NiceMock<MockMotor>* mockMotorRawPtr; // 裸指针用于 EXPECT_CALL

    // 假设丝杆螺距为 10 mm/revolution
    const double LEAD_SCREW_PITCH = 10.0; // mm/rev

    void SetUp() override {
        mockMotorUniquePtr = std::make_unique<NiceMock<MockMotor>>();
        mockMotorRawPtr = mockMotorUniquePtr.get();
        // 设置 MockMotor 的默认行为，例如 goHome() 返回 true
        ON_CALL(*mockMotorRawPtr, goHome()).WillByDefault(Return(true));
        ON_CALL(*mockMotorRawPtr, setRPM(_)).WillByDefault(Return(true)); // 默认返回true，这样不必在每个EXPECT_CALL都写
        ON_CALL(*mockMotorRawPtr, relativeMoveRevolutions(_)).WillByDefault(Return(true));
        ON_CALL(*mockMotorRawPtr, absoluteMoveRevolutions(_)).WillByDefault(Return(true));
        ON_CALL(*mockMotorRawPtr, startPositiveRPMJog()).WillByDefault(Return(true));
        ON_CALL(*mockMotorRawPtr, startNegativeRPMJog()).WillByDefault(Return(true));
        ON_CALL(*mockMotorRawPtr, stopRPMJog()).WillByDefault(Return(true));
        ON_CALL(*mockMotorRawPtr, wait(_)).WillByDefault(Return());
    }
};

TEST_F(LinearServoAdapterTest, SetPositionSpeedConvertsMmPerSecToRPM) {
    // 预期：50 mm/s / 10 mm/rev * 60 s/min = 300 RPM
    // 这里只调用了 setPositionSpeed，所以 setRPM 只会被调用一次。
    EXPECT_CALL(*mockMotorRawPtr, setRPM(300.0)).WillOnce(Return(true));

    LinearServoAdapter adapter(std::move(mockMotorUniquePtr), LEAD_SCREW_PITCH);
    ASSERT_TRUE(adapter.setPositionSpeed(50.0));
}

TEST_F(LinearServoAdapterTest, RelativeMoveConvertsMmToRevolutions) {
    // 在这个测试中，
    // 1. adapter.setPositionSpeed(50.0) 会调用 motor_->setRPM() 一次。
    // 2. adapter.relativeMove(20.0) 会在内部再次调用 motor_->setRPM() 一次。
    // 所以 setRPM 预期被调用两次。
    EXPECT_CALL(*mockMotorRawPtr, setRPM(_)).Times(2); // <-- 修改点：期望 setRPM 被调用两次
    EXPECT_CALL(*mockMotorRawPtr, relativeMoveRevolutions(2.0)).WillOnce(Return(true));

    LinearServoAdapter adapter(std::move(mockMotorUniquePtr), LEAD_SCREW_PITCH);
    adapter.setPositionSpeed(50.0); // 必须先设置速度，因为 relativeMove 使用内部速度
    ASSERT_TRUE(adapter.relativeMove(20.0));
}

TEST_F(LinearServoAdapterTest, AbsoluteMoveConvertsMmToRevolutions) {
    // 与 RelativeMove 类似，setRPM 会被调用两次。
    EXPECT_CALL(*mockMotorRawPtr, setRPM(_)).Times(2); // <-- 修改点：期望 setRPM 被调用两次
    EXPECT_CALL(*mockMotorRawPtr, absoluteMoveRevolutions(5.0)).WillOnce(Return(true));

    LinearServoAdapter adapter(std::move(mockMotorUniquePtr), LEAD_SCREW_PITCH);
    adapter.setPositionSpeed(50.0);
    ASSERT_TRUE(adapter.absoluteMove(50.0));
}

TEST_F(LinearServoAdapterTest, GoHomeCallsMotorGoHome) {
    EXPECT_CALL(*mockMotorRawPtr, goHome()).WillOnce(Return(true));

    LinearServoAdapter adapter(std::move(mockMotorUniquePtr), LEAD_SCREW_PITCH);
    ASSERT_TRUE(adapter.goHome());
}

// --- RotaryServoAdapter Tests ---
class RotaryServoAdapterTest : public ::testing::Test {
protected:
    std::unique_ptr<NiceMock<MockMotor>> mockMotorUniquePtr;
    NiceMock<MockMotor>* mockMotorRawPtr;

    // 假设每圈 360 度
    const double DEGREES_PER_REVOLUTION = 360.0;

    void SetUp() override {
        mockMotorUniquePtr = std::make_unique<NiceMock<MockMotor>>();
        mockMotorRawPtr = mockMotorUniquePtr.get();
        ON_CALL(*mockMotorRawPtr, goHome()).WillByDefault(Return(true));
        ON_CALL(*mockMotorRawPtr, setRPM(_)).WillByDefault(Return(true));
        ON_CALL(*mockMotorRawPtr, relativeMoveRevolutions(_)).WillByDefault(Return(true));
        ON_CALL(*mockMotorRawPtr, absoluteMoveRevolutions(_)).WillByDefault(Return(true));
        ON_CALL(*mockMotorRawPtr, startPositiveRPMJog()).WillByDefault(Return(true));
        ON_CALL(*mockMotorRawPtr, startNegativeRPMJog()).WillByDefault(Return(true));
        ON_CALL(*mockMotorRawPtr, stopRPMJog()).WillByDefault(Return(true));
        ON_CALL(*mockMotorRawPtr, wait(_)).WillByDefault(Return());
    }
};

TEST_F(RotaryServoAdapterTest, SetAngularPositionSpeedConvertsDegreesPerSecToRPM) {
    // 预期：180 deg/s / 360 deg/rev * 60 s/min = 30 RPM
    // 这里只调用了 setAngularPositionSpeed，所以 setRPM 只会被调用一次。
    EXPECT_CALL(*mockMotorRawPtr, setRPM(30.0)).WillOnce(Return(true));

    RotaryServoAdapter adapter(std::move(mockMotorUniquePtr), DEGREES_PER_REVOLUTION);
    ASSERT_TRUE(adapter.setAngularPositionSpeed(180.0));
}

TEST_F(RotaryServoAdapterTest, AngularMoveConvertsDegreesToRevolutions) {
    // 与 LinearServoAdapter 的移动测试类似，setRPM 会被调用两次。
    EXPECT_CALL(*mockMotorRawPtr, setRPM(_)).Times(2); // <-- 修改点：期望 setRPM 被调用两次
    EXPECT_CALL(*mockMotorRawPtr, relativeMoveRevolutions(0.25)).WillOnce(Return(true));

    RotaryServoAdapter adapter(std::move(mockMotorUniquePtr), DEGREES_PER_REVOLUTION);
    adapter.setAngularPositionSpeed(180.0);
    ASSERT_TRUE(adapter.angularMove(90.0));
}

// 确保在 tests/CMakeLists.txt 中添加这个新的测试文件
