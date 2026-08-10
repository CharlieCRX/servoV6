// ============================================================================
// test_read_result.cpp —— Step 1 contracts: ReadResult<T>
// ============================================================================
// 红：本文件引用的 infrastructure/plc_vnext/contracts/ReadResult.h 尚不存在，
//     预期编译失败；实现后应全绿。
// 用于"读取/解码失败"，与写通道的 CommunicationResult 区分。
// ============================================================================
#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/contracts/ReadResult.h"

namespace plc_vnext::contracts {
namespace {

TEST(ReadResultTest, Success_ContainsValue) {
    auto r = ReadResult<int>::success(42);
    EXPECT_TRUE(r.hasValue());
    EXPECT_EQ(r.value(), 42);
    // 不可变访问：value() 返回 const&
    const auto& ref = r.value();
    EXPECT_EQ(ref, 42);
}

TEST(ReadResultTest, Failure_ContainsKindAndDiagnostic) {
    auto r = ReadResult<int>::failure(
        ReadResult<int>::FailureKind::Decode, "register count too small");
    EXPECT_FALSE(r.hasValue());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.failureKind(), ReadResult<int>::FailureKind::Decode);
    EXPECT_EQ(r.diagnostic(), "register count too small");
}

TEST(ReadResultTest, Failure_AllKindsConstructible) {
    EXPECT_EQ(ReadResult<int>::failure(ReadResult<int>::FailureKind::Transport, "t")
                  .failureKind(),
              ReadResult<int>::FailureKind::Transport);
    EXPECT_EQ(ReadResult<int>::failure(ReadResult<int>::FailureKind::Decode, "d")
                  .failureKind(),
              ReadResult<int>::FailureKind::Decode);
    EXPECT_EQ(
        ReadResult<int>::failure(ReadResult<int>::FailureKind::RevisionChanged, "r")
            .failureKind(),
        ReadResult<int>::FailureKind::RevisionChanged);
}

TEST(ReadResultTest, NoValueOnFailure) {
    // 读取失败时，调用方绝不能误取默认值
    auto r = ReadResult<int>::failure(ReadResult<int>::FailureKind::Transport, "err");
    EXPECT_FALSE(r.hasValue());
    EXPECT_THROW(r.value(), std::logic_error);
}

TEST(ReadResultTest, MoveOutValue_OnSuccess) {
    auto r = ReadResult<std::string>::success("snapshot");
    EXPECT_TRUE(r.hasValue());
    EXPECT_EQ(r.value(), "snapshot");
}

}  // namespace
}  // namespace plc_vnext::contracts
