// tests/infrastructure/protocol/test_register_registry.cpp
#include <gtest/gtest.h>
#include "infrastructure/plc/protocol/RegisterRegistry.h"
#include "infrastructure/plc/protocol/RegisterAddressAll.h" // 复用你之前写好的 Y 轴真实寄存器

using namespace plc::protocol;

class RegisterRegistryTest : public ::testing::Test {
protected:
    RegisterRegistry registry;

    // 准备一些模拟的测试数据
    RegisterInfo coil1 = { RegisterArea::Coil, 1, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "Coil 1", 0, std::nullopt };
    RegisterInfo coil2 = { RegisterArea::Coil, 2, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "Coil 2", 0, std::nullopt };
    RegisterInfo holding1 = { RegisterArea::HoldingReg, 100, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "Holding 1", 0, std::nullopt };
    RegisterInfo holding2 = { RegisterArea::HoldingReg, 102, RegisterType::Int16, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Parameter, "", "Holding 2", 0, std::nullopt };
};

// --- 测试 1: 基础添加与全量获取 ---
TEST_F(RegisterRegistryTest, InitiallyEmpty) {
    EXPECT_TRUE(registry.all().empty());
}

TEST_F(RegisterRegistryTest, AddSingleRegister) {
    registry.add(coil1);
    ASSERT_EQ(registry.all().size(), 1);
    EXPECT_EQ(registry.all()[0].address, 1);
}

TEST_F(RegisterRegistryTest, AddMultipleRegistersUsingSpan) {
    std::vector<RegisterInfo> regs = { coil1, holding1 };
    registry.addAll(regs);
    EXPECT_EQ(registry.all().size(), 2);
}

// --- 测试 2: 维度查询 - 按 Group 分组 (支撑未来的 PollingScheduler) ---
TEST_F(RegisterRegistryTest, FindByGroup_FiltersCorrectly) {
    registry.addAll({ coil1, coil2, holding1, holding2 });
    
    // 查询 Command 组
    auto commands = registry.findByGroup(RegisterGroup::Command);
    ASSERT_EQ(commands.size(), 2);
    EXPECT_EQ(commands[0].address, 1);
    EXPECT_EQ(commands[1].address, 2);

    // 查询 Feedback 组
    auto feedbacks = registry.findByGroup(RegisterGroup::Feedback);
    ASSERT_EQ(feedbacks.size(), 1);
    EXPECT_EQ(feedbacks[0].address, 100);
}

// --- 测试 3: 维度查询 - 按 Area 分区 (支撑协议分发) ---
TEST_F(RegisterRegistryTest, FindByArea_FiltersCorrectly) {
    registry.addAll({ coil1, coil2, holding1, holding2 });
    
    auto coils = registry.findByArea(RegisterArea::Coil);
    ASSERT_EQ(coils.size(), 2);

    auto holdings = registry.findByArea(RegisterArea::HoldingReg);
    ASSERT_EQ(holdings.size(), 2);
}

// --- 测试 4: 精确检索 - 按 地址 (支撑 FeedbackDecoder 和 Map 映射) ---
TEST_F(RegisterRegistryTest, FindByAddress_ReturnsPointerToCorrectRegister) {
    registry.addAll({ coil1, holding1 });
    
    const auto* found = registry.findByAddress(RegisterArea::HoldingReg, 100);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(std::string(found->description), "Holding 1");
}

TEST_F(RegisterRegistryTest, FindByAddress_WrongArea_ReturnsNullptr) {
    registry.addAll({ coil1, holding1 });
    
    // 地址 100 是 HoldingReg，如果在 Coil 区查，应该返回空
    const auto* found = registry.findByAddress(RegisterArea::Coil, 100);
    EXPECT_EQ(found, nullptr);
}

TEST_F(RegisterRegistryTest, FindByAddress_NonExistent_ReturnsNullptr) {
    registry.addAll({ coil1, holding1 });
    const auto* found = registry.findByAddress(RegisterArea::HoldingReg, 999);
    EXPECT_EQ(found, nullptr);
}

// --- 测试 5: 真实场景集成验证 (接入 RegisterAddressY) ---
TEST_F(RegisterRegistryTest, IntegrationWithRealAddressDefinitions) {
    // 将真实定义的寄存器加入 Registry
    registry.add(plc::reg::y_axis::command::ENABLE_REQUEST);
    registry.add(plc::reg::y_axis::command::ABS_TARGET);
    registry.add(plc::reg::y_axis::feedback::STATE);
    
    auto feedbacks = registry.findByGroup(RegisterGroup::Feedback);
    ASSERT_EQ(feedbacks.size(), 1);
    EXPECT_EQ(feedbacks[0].address, 101); // feedback::STATE 的地址
}