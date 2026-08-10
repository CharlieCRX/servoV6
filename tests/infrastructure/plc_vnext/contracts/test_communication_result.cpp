// ============================================================================
// test_communication_result.cpp —— Step 1 contracts: CommunicationResult
// ============================================================================
// 红：本文件引用的 infrastructure/plc_vnext/contracts/CommunicationResult.h
//     尚不存在，预期编译失败；实现后应全绿。
// 语义与旧 infrastructure/ISystemDriver.h 的 CommunicationResult 逐字段一致，
// 但零 include 依赖。
// ============================================================================
#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"

namespace plc_vnext::contracts {
namespace {

TEST(CommunicationResultTest, Sent_IsOkNotRetryable) {
    auto r = CommunicationResult::sent();
    EXPECT_TRUE(r.ok());
    EXPECT_FALSE(r.retryable());
    EXPECT_FALSE(r.isNetworkIssue());
}

TEST(CommunicationResultTest, Timeout_IsRetryable) {
    CommunicationResult r;
    r.status = CommunicationResult::Status::Timeout;
    EXPECT_TRUE(r.retryable());
    EXPECT_TRUE(r.isNetworkIssue());
    EXPECT_FALSE(r.ok());
}

TEST(CommunicationResultTest, Busy_IsRetryable) {
    CommunicationResult r;
    r.status = CommunicationResult::Status::Busy;
    EXPECT_TRUE(r.retryable());
    EXPECT_FALSE(r.isNetworkIssue());
}

TEST(CommunicationResultTest, NetworkError_NotRetryable_IsNetworkIssue) {
    CommunicationResult r;
    r.status = CommunicationResult::Status::NetworkError;
    EXPECT_FALSE(r.retryable());
    EXPECT_TRUE(r.isNetworkIssue());
    EXPECT_FALSE(r.ok());
}

TEST(CommunicationResultTest, Disconnected_NotRetryable_IsNetworkIssue) {
    auto r = CommunicationResult::disconnected("socket not connected");
    EXPECT_EQ(r.status, CommunicationResult::Status::Disconnected);
    EXPECT_FALSE(r.retryable());
    EXPECT_TRUE(r.isNetworkIssue());
    EXPECT_EQ(r.diagnostic, "socket not connected");
}

TEST(CommunicationResultTest, ProtocolError_KeepsExceptionCode) {
    CommunicationResult r;
    r.status = CommunicationResult::Status::ProtocolError;
    r.exceptionCode = 0x02;  // Illegal Address
    EXPECT_FALSE(r.retryable());
    EXPECT_FALSE(r.isNetworkIssue());
    EXPECT_EQ(r.exceptionCode, 0x02);
    EXPECT_TRUE(r.isProtocolIssue());
    EXPECT_FALSE(r.isDisconnected());
}

TEST(CommunicationResultTest, IndependentFromISystemDriver) {
    // 本文件只 include contracts/CommunicationResult.h 即可编译，
    // 不 include "infrastructure/ISystemDriver.h"；能编译即验证解耦。
    CommunicationResult r;
    EXPECT_TRUE(r.ok());  // 默认状态为 Sent
}

}  // namespace
}  // namespace plc_vnext::contracts
