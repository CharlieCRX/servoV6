// ============================================================================
// test_command_write_policy.cpp —— Step 8 command: 写入分类（红）
// ============================================================================
// 依据《TDD实施文档》Step 8 红测试：
//   LevelHold_Classified    保持电平命令可重复写（使能/点动/心跳）
//   SelfReset_Classified    PLC 自复位命令只写 ON（触发/终止/清除）
// 纯函数测试，无 I/O。
// ============================================================================
#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/command/CommandWritePolicy.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"

namespace plc_vnext {
namespace {

using command::CommandWriteClass;
using command::CommandWritePolicy;
using contracts::PlcAxisCommandKind;

// ─────────────────────────────────────────────
// 保持电平命令可重复写
// ─────────────────────────────────────────────
TEST(CommandWritePolicyTest, LevelHold_Classified) {
    EXPECT_EQ(CommandWritePolicy::classify(PlcAxisCommandKind::EnableAxis),
              CommandWriteClass::LevelHold);
    EXPECT_TRUE(CommandWritePolicy::isLevelHold(PlcAxisCommandKind::EnableAxis));
    EXPECT_TRUE(CommandWritePolicy::isLevelHold(PlcAxisCommandKind::EnableMotor));
    EXPECT_TRUE(CommandWritePolicy::isLevelHold(PlcAxisCommandKind::JogForward));
    EXPECT_TRUE(CommandWritePolicy::isLevelHold(PlcAxisCommandKind::JogBackward));
    EXPECT_TRUE(CommandWritePolicy::isLevelHold(PlcAxisCommandKind::JogHeartbeat));

    // 参数写（REAL 保持寄存器）同样可重复写，归入 LevelHold。
    EXPECT_TRUE(CommandWritePolicy::isLevelHold(PlcAxisCommandKind::SetManualSpeed));
    EXPECT_TRUE(CommandWritePolicy::isLevelHold(PlcAxisCommandKind::SetAbsTarget));
}

// ─────────────────────────────────────────────
// 触发/终止/清除命令：PLC 自复位，只写 ON
// ─────────────────────────────────────────────
TEST(CommandWritePolicyTest, SelfReset_Classified) {
    // 触发/终止线圈由 PLC 当前版本自动复位，归入 SelfReset（只写 ON）。
    EXPECT_TRUE(CommandWritePolicy::isSelfReset(PlcAxisCommandKind::TriggerAbsMove));
    EXPECT_TRUE(CommandWritePolicy::isSelfReset(PlcAxisCommandKind::TriggerRelMove));
    EXPECT_TRUE(CommandWritePolicy::isSelfReset(PlcAxisCommandKind::StopAbsMove));
    EXPECT_TRUE(CommandWritePolicy::isSelfReset(PlcAxisCommandKind::StopRelMove));
    // 清除/原点命令同样 PLC 自复位。
    EXPECT_TRUE(CommandWritePolicy::isSelfReset(PlcAxisCommandKind::ClearAbsPosition));
    EXPECT_TRUE(CommandWritePolicy::isSelfReset(PlcAxisCommandKind::ClearRelZero));
    EXPECT_TRUE(CommandWritePolicy::isSelfReset(PlcAxisCommandKind::SetRelZero));

    EXPECT_EQ(CommandWritePolicy::classify(PlcAxisCommandKind::TriggerAbsMove),
              CommandWriteClass::SelfReset);
    EXPECT_EQ(CommandWritePolicy::classify(PlcAxisCommandKind::ClearRelZero),
              CommandWriteClass::SelfReset);
    EXPECT_FALSE(CommandWritePolicy::isLevelHold(PlcAxisCommandKind::TriggerAbsMove));
}

}  // namespace
}  // namespace plc_vnext
