#include <gtest/gtest.h>
#include "infrastructure/plc/protocol/RegisterAddressY.h"

using namespace plc::reg::y_axis;
using namespace plc::protocol;

// 约束测试 1：验证 DRY 原则，类型必须正确推导长度
TEST(ProtocolConstraintTest, AutoWordCount_IsCorrect) {
    EXPECT_EQ(command::ENABLE_REQUEST.wordCount(), 1); // Bool -> 1
    EXPECT_EQ(feedback::STATE.wordCount(), 1);         // Int16 -> 1
    EXPECT_EQ(command::ABS_TARGET.wordCount(), 2);     // Float32 -> 2
}

// 约束测试 2：边沿触发器必须定义脉冲宽度
TEST(ProtocolConstraintTest, ManualEdgeTrigger_MustHavePulseWidth) {
    auto checkPulseWidth = [](const RegisterInfo& info) {
        if (info.behavior == RegisterBehavior::ManualResetEdgeTrigger) {
            EXPECT_GT(info.pulseWidthMs, 0) 
                << "Error in " << info.description << ": Manual reset triggers must have a pulse width > 0";
        } else {
            EXPECT_EQ(info.pulseWidthMs, 0) 
                << "Error in " << info.description << ": Only manual reset triggers should define a pulse width";
        }
    };

    checkPulseWidth(command::ABS_MOVE_TRIGGER);
    checkPulseWidth(command::ENABLE_REQUEST);
}

// 约束测试 3：验证命令区和状态区的界限
TEST(ProtocolConstraintTest, SeparationOfCommandAndState) {
    // 业务层发送的是 Enable Request
    EXPECT_EQ(command::ENABLE_REQUEST.group, RegisterGroup::Command);
    EXPECT_EQ(command::ENABLE_REQUEST.access, RegisterAccess::ReadWrite);

    // 业务层读取的是真实的 State
    EXPECT_EQ(feedback::STATE.group, RegisterGroup::Feedback);
    EXPECT_EQ(feedback::STATE.access, RegisterAccess::ReadOnly);
}