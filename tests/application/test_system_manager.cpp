#include <gtest/gtest.h>
#include "application/SystemManager.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/ContextRejection.h"

// ============================================================
// SystemManager TDD 测试套件
// 基于 Try-Get + ContextRejection 错误码模式
// ============================================================

class SystemManagerTest : public ::testing::Test {
protected:
    SystemManager manager;
    
    // 辅助：断言错误码匹配
    void expectRejection(ContextRejection expected, ContextRejection actual) {
        EXPECT_EQ(expected, actual);
    }
};

// ============================================================
// 一、createGroup — 成功路径
// ============================================================

TEST_F(SystemManagerTest, CreateGroup_Success_ReturnsTrue) {
    ContextRejection reason = ContextRejection::None;
    bool ok = manager.createGroup("GroupA", reason);
    
    EXPECT_TRUE(ok);
    EXPECT_EQ(reason, ContextRejection::None);
}

TEST_F(SystemManagerTest, CreateGroup_Success_ThenTryGetReturnsIt) {
    ContextRejection reason = ContextRejection::None;
    ASSERT_TRUE(manager.createGroup("GroupA", reason));
    
    SystemContext* group = nullptr;
    bool found = manager.tryGetGroup("GroupA", group, reason);
    
    EXPECT_TRUE(found);
    EXPECT_NE(group, nullptr);
    EXPECT_EQ(reason, ContextRejection::None);
}

// ============================================================
// 二、createGroup — 错误路径：空名称
// ============================================================

TEST_F(SystemManagerTest, CreateGroup_EmptyName_ReturnsFalseWithGroupNameInvalid) {
    ContextRejection reason = ContextRejection::None;
    bool ok = manager.createGroup("", reason);
    
    EXPECT_FALSE(ok);
    EXPECT_EQ(reason, ContextRejection::GroupNameInvalid);
}

TEST_F(SystemManagerTest, CreateGroup_EmptyName_DoesNotCreateGroup) {
    ContextRejection reason = ContextRejection::None;
    manager.createGroup("", reason);
    
    SystemContext* group = nullptr;
    bool found = manager.tryGetGroup("", group, reason);
    
    EXPECT_FALSE(found);
    // 空名称被 tryGetGroup 的入参校验拦截，返回 GroupNameInvalid
    EXPECT_EQ(reason, ContextRejection::GroupNameInvalid);
}

// ============================================================
// 三、createGroup — 错误路径：重复名称
// ============================================================

TEST_F(SystemManagerTest, CreateGroup_DuplicateName_ReturnsFalseWithGroupAlreadyExists) {
    ContextRejection reason = ContextRejection::None;
    ASSERT_TRUE(manager.createGroup("GroupA", reason));
    
    // 第二次创建同名分组
    bool ok = manager.createGroup("GroupA", reason);
    
    EXPECT_FALSE(ok);
    EXPECT_EQ(reason, ContextRejection::GroupAlreadyExists);
}

TEST_F(SystemManagerTest, CreateGroup_DuplicateName_OriginalGroupRemainsIntact) {
    ContextRejection reason = ContextRejection::None;
    ASSERT_TRUE(manager.createGroup("GroupA", reason));
    
    // 在原始分组上做状态变更
    SystemContext* a = nullptr;
    ASSERT_TRUE(manager.tryGetGroup("GroupA", a, reason));
    a->gantry().applyFeedback({.isCoupled = false, .errorCode = 0});  // 解耦
    
    // 重复创建应失败
    manager.createGroup("GroupA", reason);
    
    // 原始分组的状态不应被影响
    SystemContext* aAgain = nullptr;
    ASSERT_TRUE(manager.tryGetGroup("GroupA", aAgain, reason));
    EXPECT_FALSE(aAgain->gantry().isCoupled()) << "原始分组状态应保持不变";
}

// ============================================================
// 四、tryGetGroup — 成功路径
// ============================================================

TEST_F(SystemManagerTest, TryGetGroup_ExistingGroup_ReturnsTrueAndNonNullPointer) {
    ContextRejection reason = ContextRejection::None;
    manager.createGroup("GroupA", reason);
    
    SystemContext* group = nullptr;
    bool found = manager.tryGetGroup("GroupA", group, reason);
    
    EXPECT_TRUE(found);
    EXPECT_NE(group, nullptr);
    EXPECT_EQ(reason, ContextRejection::None);
}

TEST_F(SystemManagerTest, TryGetGroup_ExistingGroup_ReasonIsNone) {
    ContextRejection reason = ContextRejection::GroupNotFound; // 预置一个错误值
    manager.createGroup("GroupA", reason);
    
    SystemContext* group = nullptr;
    manager.tryGetGroup("GroupA", group, reason);
    
    // 成功时应覆盖为 None
    EXPECT_EQ(reason, ContextRejection::None);
}

// ============================================================
// 五、tryGetGroup — 错误路径：不存在的分组
// ============================================================

TEST_F(SystemManagerTest, TryGetGroup_NonExistent_ReturnsFalseWithGroupNotFound) {
    SystemContext* group = nullptr;
    ContextRejection reason = ContextRejection::None;
    
    bool found = manager.tryGetGroup("NonExistent", group, reason);
    
    EXPECT_FALSE(found);
    EXPECT_EQ(group, nullptr);
    EXPECT_EQ(reason, ContextRejection::GroupNotFound);
}

TEST_F(SystemManagerTest, TryGetGroup_NonExistent_OutGroupIsNullptr) {
    SystemContext* group = reinterpret_cast<SystemContext*>(0xDEADBEEF); // 非空哑值
    ContextRejection reason = ContextRejection::None;
    
    manager.tryGetGroup("Ghost", group, reason);
    
    // 失败时必须将 outGroup 置为 nullptr，避免调用方使用野指针
    EXPECT_EQ(group, nullptr);
}

// ============================================================
// 六、removeGroup 后查询
// ============================================================

TEST_F(SystemManagerTest, RemoveGroup_ThenTryGetReturnsFalse) {
    ContextRejection reason = ContextRejection::None;
    manager.createGroup("GroupA", reason);
    
    manager.removeGroup("GroupA");
    
    SystemContext* group = nullptr;
    bool found = manager.tryGetGroup("GroupA", group, reason);
    
    EXPECT_FALSE(found);
    EXPECT_EQ(reason, ContextRejection::GroupNotFound);
}

TEST_F(SystemManagerTest, RemoveGroup_ThenRecreateWithSameNameSucceeds) {
    ContextRejection reason = ContextRejection::None;
    manager.createGroup("GroupA", reason);
    manager.removeGroup("GroupA");
    
    // 删除后重新创建同名分组应成功（ID 可复用）
    bool ok = manager.createGroup("GroupA", reason);
    
    EXPECT_TRUE(ok);
    EXPECT_EQ(reason, ContextRejection::None);
    
    SystemContext* group = nullptr;
    bool found = manager.tryGetGroup("GroupA", group, reason);
    EXPECT_TRUE(found);
    EXPECT_NE(group, nullptr);
}

// ============================================================
// 七、多分组隔离（平行宇宙原则）
// ============================================================

TEST_F(SystemManagerTest, MultiGroup_Isolation_DifferentMemoryAddresses) {
    ContextRejection reason = ContextRejection::None;
    manager.createGroup("Group_A", reason);
    manager.createGroup("Group_B", reason);
    
    SystemContext* a = nullptr;
    SystemContext* b = nullptr;
    manager.tryGetGroup("Group_A", a, reason);
    manager.tryGetGroup("Group_B", b, reason);
    
    EXPECT_NE(a, b) << "不同分组必须拥有独立的 SystemContext 实例";
}

TEST_F(SystemManagerTest, MultiGroup_StateIsolation_MutualIndependence) {
    ContextRejection reason = ContextRejection::None;
    manager.createGroup("Group_A", reason);
    manager.createGroup("Group_B", reason);
    
    SystemContext* a = nullptr;
    SystemContext* b = nullptr;
    manager.tryGetGroup("Group_A", a, reason);
    manager.tryGetGroup("Group_B", b, reason);
    
    // A 解耦
    a->gantry().applyFeedback({.isCoupled = false, .errorCode = 0});
    // B 保持联动
    b->gantry().applyFeedback({.isCoupled = true, .errorCode = 0});
    
    EXPECT_FALSE(a->gantry().isCoupled());
    EXPECT_TRUE(b->gantry().isCoupled());
}

// ============================================================
// 八、Try-Get 模式一致性：成功时 outReason 必须为 None
// ============================================================

TEST_F(SystemManagerTest, TryGet_SuccessOverwritesReasonToNone) {
    ContextRejection reason = ContextRejection::GroupNotFound;
    manager.createGroup("GroupA", reason);
    
    SystemContext* group = nullptr;
    bool found = manager.tryGetGroup("GroupA", group, reason);
    
    EXPECT_TRUE(found);
    // reason 必须是 None，即使传入前是其他值
    EXPECT_EQ(reason, ContextRejection::None);
}

// ============================================================
// 九、边界场景：空管理器下的各种操作
// ============================================================

TEST_F(SystemManagerTest, FreshManager_TryGetAny_ReturnsFalse) {
    SystemContext* group = nullptr;
    ContextRejection reason = ContextRejection::None;
    
    bool found = manager.tryGetGroup("Anything", group, reason);
    
    EXPECT_FALSE(found);
    EXPECT_EQ(reason, ContextRejection::GroupNotFound);
}
