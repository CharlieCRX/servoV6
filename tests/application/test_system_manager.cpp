#include <gtest/gtest.h>
#include "application/SystemManager.h"
#include "domain/entity/SystemContext.h"

class SystemManagerTest : public ::testing::Test {
protected:
    SystemManager manager;
};

// 1. 测试创建与存在性
TEST_F(SystemManagerTest, CreateAndGetGroup_Success) {
    const std::string groupName = "Machine_A";
    manager.createGroup(groupName);

    SystemContext* group = manager.getGroup(groupName);
    EXPECT_NE(group, nullptr);
}

// 2. 测试获取不存在的分组应返回 nullptr (不抛异常)
TEST_F(SystemManagerTest, GetNonExistentGroup_ReturnsNull) {
    SystemContext* group = manager.getGroup("NonExistent");
    EXPECT_EQ(group, nullptr);
}

// 3. 测试多实例隔离（核心：平行宇宙原则）
TEST_F(SystemManagerTest, MultiGroup_Isolation) {
    manager.createGroup("Group_A");
    manager.createGroup("Group_B");

    SystemContext* a = manager.getGroup("Group_A");
    SystemContext* b = manager.getGroup("Group_B");

    // 内存地址必须不同
    EXPECT_NE(a, b);

    // 状态修改互不影响
    a->setCoupledState(false); // A 解耦
    b->setCoupledState(true);  // B 保持联动

    EXPECT_FALSE(a->isGantryCoupled());
    EXPECT_TRUE(b->isGantryCoupled());
}

// 4. 测试重复创建冲突 (语义：ID 唯一性)
TEST_F(SystemManagerTest, CreateDuplicateGroup_ShouldFail) {
    EXPECT_TRUE(manager.createGroup("Station_1"));
    EXPECT_FALSE(manager.createGroup("Station_1")); // 第二次创建应返回 false
}