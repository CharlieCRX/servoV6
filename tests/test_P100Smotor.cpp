#include "gtest/gtest.h"
#include "IMotor.h" // 包含 IMotor 接口

// 临时的 P100SMotor 假实现，用于让测试编译通过并失败
class P100SMotor_Fake : public IMotor {
public:
    // 所有方法都返回 false，确保测试最初会失败
    bool setRPM(double rpm) override { return false; }
    bool relativeMoveRevolutions(double revolutions) override { return false; }
    bool absoluteMoveRevolutions(double targetRevolutions) override { return false; }
    bool startPositiveRPMJog() override { return false; }
    bool startNegativeRPMJog() override { return false; }
    bool stopRPMJog() override { return false; }
    bool goHome() override { return false; }
    void wait(int ms) override {} // void 方法不需要返回 false
};

// 测试 P100SMotor_Fake 的 setRPM 方法是否按预期返回 false (Red 阶段)
TEST(P100SMotorTest, SetRPM_ReturnsFalseInitially) {
    P100SMotor_Fake motor;
    // 期望这里会失败，因为假实现返回 false
    EXPECT_FALSE(motor.setRPM(100.0));
}
