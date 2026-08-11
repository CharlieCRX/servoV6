// ============================================================================
// test_plc_axis_command_writer.cpp —— Step 8 command: 单轴命令写入（红）
// ============================================================================
// 依据《TDD实施文档》Step 8 红测试与《PLC变量协议_Modbus最终地址表.md》
// §3.1/§3.2：
//   WriteManualSpeed_EncodesRealLowWordFirst  写 D(0+2s) REAL，低字在前
//   WriteAbsTarget_SeparateFromTrigger        目标与触发是两次独立调用
//   WriteCoil_EnableAxis                      M(0+i) 保持电平写 ON/OFF
//   WriteCommand_ReportsCommResult            返回 CommunicationResult，不伪造成功
// 全部经 FakeModbusClient 离线执行；不接触真实 PLC / AsioModbusTcpClient。
// ============================================================================
#include <memory>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/command/PlcAxisCommandWriter.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/fake/FakeModbusClient.h"
#include "infrastructure/plc_vnext/layout/AxisSlotCoilLayout.h"
#include "infrastructure/plc_vnext/layout/AxisSlotRegisterLayout.h"

namespace plc_vnext {
namespace {

using contracts::CommunicationResult;
using contracts::PlcAxisCommand;
using contracts::PlcAxisSlot;

// ─────────────────────────────────────────────
// 手动速度写：REAL 编码低字在前（CDAB）
// ─────────────────────────────────────────────
TEST(PlcAxisCommandWriterTest, WriteManualSpeed_EncodesRealLowWordFirst) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcAxisCommandWriter writer(fake);

    const auto slot = *PlcAxisSlot::tryCreate(0);
    auto res = writer.write(slot, PlcAxisCommand::makeSetManualSpeed(1.0f));
    ASSERT_TRUE(res.ok()) << res.diagnostic;

    auto multi = fake->writtenMulti();
    ASSERT_EQ(multi.size(), 1u);
    EXPECT_EQ(multi[0].startAddress, layout::manualSpeed(0).value());
    ASSERT_EQ(multi[0].values.size(), 2u);
    // 1.0f = 0x3F800000 → 低字在前 {0x0000, 0x3F80}
    EXPECT_EQ(multi[0].values[0], 0x0000u);
    EXPECT_EQ(multi[0].values[1], 0x3F80u);

    // 真实落盘，可逐地址读回。
    EXPECT_EQ(*fake->readRegister(layout::manualSpeed(0).value()), 0x0000u);
    EXPECT_EQ(*fake->readRegister(layout::manualSpeed(0).value() + 1), 0x3F80u);
}

TEST(PlcAxisCommandWriterTest, WriteManualSpeed_SlotOffset_And25_8) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcAxisCommandWriter writer(fake);

    const auto slot = *PlcAxisSlot::tryCreate(3);
    auto res = writer.write(slot, PlcAxisCommand::makeSetManualSpeed(25.8f));
    ASSERT_TRUE(res.ok());

    auto multi = fake->writtenMulti();
    ASSERT_EQ(multi.size(), 1u);
    // slot3 手动速度 = D(0+2*3) = D6
    EXPECT_EQ(multi[0].startAddress, 6u);
    // 25.8f = 0x41CE6666 → 低字在前 {0x6666, 0x41CE}
    ASSERT_EQ(multi[0].values.size(), 2u);
    EXPECT_EQ(multi[0].values[0], 0x6666u);
    EXPECT_EQ(multi[0].values[1], 0x41CEu);
}

// ─────────────────────────────────────────────
// 目标与触发分离：两次独立调用，互不污染
// ─────────────────────────────────────────────
TEST(PlcAxisCommandWriterTest, WriteAbsTarget_SeparateFromTrigger) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcAxisCommandWriter writer(fake);

    const auto slot = *PlcAxisSlot::tryCreate(5);

    // 目标写：只写保持寄存器，不写线圈。
    auto t = writer.write(slot, PlcAxisCommand::makeSetAbsTarget(123.0f));
    ASSERT_TRUE(t.ok());
    ASSERT_EQ(fake->writtenMulti().size(), 1u);
    EXPECT_EQ(fake->writtenMulti()[0].startAddress, layout::absPosTarget(5).value());
    EXPECT_TRUE(fake->writtenCoils().empty());

    // 触发写：只写线圈（PLC 自复位，只写 ON），不新增保持寄存器写。
    auto g = writer.write(slot, PlcAxisCommand::makeTriggerAbsMove());
    ASSERT_TRUE(g.ok());
    EXPECT_EQ(fake->writtenMulti().size(), 1u);

    auto coils = fake->writtenCoils();
    ASSERT_EQ(coils.size(), 1u);
    EXPECT_EQ(coils[0].address, layout::triggerAbsMove(5).value());
    EXPECT_TRUE(coils[0].value);
    // 触发线圈由 PLC 自动复位，客户端只写一次 ON、无待关断状态。
    EXPECT_EQ(fake->writtenCoils().size(), 1u);
}

// ─────────────────────────────────────────────
// 使能轴控：M(0+i) 保持电平写 ON/OFF
// ─────────────────────────────────────────────
TEST(PlcAxisCommandWriterTest, WriteCoil_EnableAxis_LevelHold) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcAxisCommandWriter writer(fake);

    const auto slot = *PlcAxisSlot::tryCreate(2);

    ASSERT_TRUE(writer.write(slot, PlcAxisCommand::makeEnableAxis(true)).ok());
    auto coils = fake->writtenCoils();
    ASSERT_EQ(coils.size(), 1u);
    EXPECT_EQ(coils[0].address, layout::enableAxis(2).value());
    EXPECT_TRUE(coils[0].value);
    EXPECT_EQ(*fake->readCoil(layout::enableAxis(2).value()), true);

    ASSERT_TRUE(writer.write(slot, PlcAxisCommand::makeEnableAxis(false)).ok());
    coils = fake->writtenCoils();
    ASSERT_EQ(coils.size(), 2u);
    EXPECT_EQ(coils[1].address, layout::enableAxis(2).value());
    EXPECT_FALSE(coils[1].value);
    EXPECT_EQ(*fake->readCoil(layout::enableAxis(2).value()), false);
}

// ─────────────────────────────────────────────
// 触发命令：PLC 自动复位，只写一次 ON，无需 OFF
// ─────────────────────────────────────────────
TEST(PlcAxisCommandWriterTest, WriteTrigger_SelfResetWritesOnOnce) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcAxisCommandWriter writer(fake);
    const auto slot = *PlcAxisSlot::tryCreate(0);

    ASSERT_TRUE(writer.write(slot, PlcAxisCommand::makeTriggerAbsMove()).ok());
    auto coils = fake->writtenCoils();
    ASSERT_EQ(coils.size(), 1u);
    EXPECT_EQ(coils[0].address, layout::triggerAbsMove(0).value());
    EXPECT_TRUE(coils[0].value);
    // 触发线圈由 PLC 自动复位，客户端只写一次 ON，不负责回写 OFF。
    EXPECT_EQ(fake->writtenCoils().size(), 1u);
}

// ─────────────────────────────────────────────
// 触发命令：断线写失败不落盘、重连后不自动重写 ON（无重放）
// ─────────────────────────────────────────────
TEST(PlcAxisCommandWriterTest, WriteTrigger_NoReplayAfterDisconnect) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcAxisCommandWriter writer(fake);
    const auto slot = *PlcAxisSlot::tryCreate(0);

    fake->setConnected(false);
    auto res = writer.write(slot, PlcAxisCommand::makeTriggerAbsMove());
    EXPECT_FALSE(res.ok());
    // 断线时写 ON 失败：不落盘、不留可重放状态。
    EXPECT_TRUE(fake->writtenCoils().empty());

    // 重连后 writer 不会自动重新写 ON（无重放）。
    fake->setConnected(true);
    EXPECT_TRUE(fake->writtenCoils().empty());

    // 下一次显式触发是一次全新提交（复位完成后允许）。
    auto res2 = writer.write(slot, PlcAxisCommand::makeTriggerAbsMove());
    ASSERT_TRUE(res2.ok());
    auto coils = fake->writtenCoils();
    ASSERT_EQ(coils.size(), 1u);
    EXPECT_EQ(coils[0].address, layout::triggerAbsMove(0).value());
    EXPECT_TRUE(coils[0].value);
}

// ─────────────────────────────────────────────
// 契约：writer 只提交，不读回确认（确认由 telemetry/ack reader 异步完成）
// ─────────────────────────────────────────────
TEST(PlcAxisCommandWriterTest, WriteTrigger_DoesNotReadBack) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcAxisCommandWriter writer(fake);
    const auto slot = *PlcAxisSlot::tryCreate(0);

    auto res = writer.write(slot, PlcAxisCommand::makeTriggerAbsMove());
    ASSERT_TRUE(res.ok());
    // 只提交一次 ON；未发生任何读操作（writer 不做读回确认）。
    EXPECT_EQ(fake->readCount(), 0u);
    EXPECT_EQ(fake->writtenCoils().size(), 1u);
}

// ─────────────────────────────────────────────
// 写入必须如实报告 CommunicationResult，不伪造成功
// ─────────────────────────────────────────────
TEST(PlcAxisCommandWriterTest, WriteCommand_ReportsCommResult_NoFakeSuccess) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    fake->scriptTransportFailure(CommunicationResult::Status::NetworkError,
                                 "simulated cable pull");
    command::PlcAxisCommandWriter writer(fake);

    const auto slot = *PlcAxisSlot::tryCreate(1);
    auto res = writer.write(slot, PlcAxisCommand::makeEnableAxis(true));
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status, CommunicationResult::Status::NetworkError);
    // 失败不落盘、不留写记录。
    EXPECT_TRUE(fake->writtenCoils().empty());
}

}  // namespace
}  // namespace plc_vnext
