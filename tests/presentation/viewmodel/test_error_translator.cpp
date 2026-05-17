#include <gtest/gtest.h>
#include <variant>
#include "presentation/viewmodel/ErrorTranslator.h"
#include "domain/entity/Axis.h"
#include "domain/entity/ContextRejection.h"
#include "domain/gantry/GantryRejection.h"
#include "domain/safety/SafetyRejection.h"
#include "infrastructure/ISystemDriver.h"

// ============================================================================
// ErrorTranslator 测试 — 覆盖 UseCaseError 所有 variant 分支
// ============================================================================

class ErrorTranslatorTest : public ::testing::Test {
protected:
};

// ============================================================================
// 1. 成功态（monostate）
// ============================================================================

TEST_F(ErrorTranslatorTest, MonostateShouldReturnEmptyError) {
    UseCaseError err = std::monostate{};
    auto vmErr = translate(err);
    EXPECT_FALSE(vmErr.isValid());
    EXPECT_TRUE(vmErr.code.empty());
    EXPECT_TRUE(vmErr.userMessage.empty());
    EXPECT_EQ(vmErr.category, ErrorCategory::Silent);
}

// ============================================================================
// 2. RejectionReason（Axis 领域层）
// ============================================================================

TEST_F(ErrorTranslatorTest, RejectionReason_InvalidState) {
    UseCaseError err = RejectionReason::InvalidState;
    auto vmErr = translate(err);
    EXPECT_EQ(vmErr.code, "AXIS_INVALID_STATE");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
    EXPECT_TRUE(vmErr.isValid());
}

TEST_F(ErrorTranslatorTest, RejectionReason_AlreadyMoving) {
    auto vmErr = translate(UseCaseError{RejectionReason::AlreadyMoving});
    EXPECT_EQ(vmErr.code, "AXIS_ALREADY_MOVING");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}

TEST_F(ErrorTranslatorTest, RejectionReason_TargetOutOfPositiveLimit) {
    auto vmErr = translate(UseCaseError{RejectionReason::TargetOutOfPositiveLimit});
    EXPECT_EQ(vmErr.code, "AXIS_TARGET_OUT_OF_POS_LIMIT");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}

TEST_F(ErrorTranslatorTest, RejectionReason_TargetOutOfNegativeLimit) {
    auto vmErr = translate(UseCaseError{RejectionReason::TargetOutOfNegativeLimit});
    EXPECT_EQ(vmErr.code, "AXIS_TARGET_OUT_OF_NEG_LIMIT");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}

TEST_F(ErrorTranslatorTest, RejectionReason_AtPositiveLimit) {
    auto vmErr = translate(UseCaseError{RejectionReason::AtPositiveLimit});
    EXPECT_EQ(vmErr.code, "AXIS_AT_POSITIVE_LIMIT");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}

TEST_F(ErrorTranslatorTest, RejectionReason_AtNegativeLimit) {
    auto vmErr = translate(UseCaseError{RejectionReason::AtNegativeLimit});
    EXPECT_EQ(vmErr.code, "AXIS_AT_NEGATIVE_LIMIT");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}

TEST_F(ErrorTranslatorTest, RejectionReason_InvalidArgument) {
    auto vmErr = translate(UseCaseError{RejectionReason::InvalidArgument});
    EXPECT_EQ(vmErr.code, "AXIS_INVALID_ARGUMENT");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}

TEST_F(ErrorTranslatorTest, RejectionReason_UnknownError) {
    auto vmErr = translate(UseCaseError{RejectionReason::UnknownError});
    EXPECT_EQ(vmErr.code, "AXIS_UNKNOWN_ERROR");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, RejectionReason_None) {
    auto vmErr = translate(UseCaseError{RejectionReason::None});
    EXPECT_EQ(vmErr.code, "AXIS_UNKNOWN_ERROR");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

// ============================================================================
// 3. ContextRejection（SystemManager / SystemContext 层）
// ============================================================================

TEST_F(ErrorTranslatorTest, ContextRejection_GroupNotFound) {
    auto vmErr = translate(UseCaseError{ContextRejection::GroupNotFound});
    EXPECT_EQ(vmErr.code, "CTX_GROUP_NOT_FOUND");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, ContextRejection_GroupAlreadyExists) {
    auto vmErr = translate(UseCaseError{ContextRejection::GroupAlreadyExists});
    EXPECT_EQ(vmErr.code, "CTX_GROUP_ALREADY_EXISTS");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, ContextRejection_GroupNameInvalid) {
    auto vmErr = translate(UseCaseError{ContextRejection::GroupNameInvalid});
    EXPECT_EQ(vmErr.code, "CTX_GROUP_NAME_INVALID");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, ContextRejection_PhysicalAxisLockedByGantry) {
    auto vmErr = translate(UseCaseError{ContextRejection::PhysicalAxisLockedByGantry});
    EXPECT_EQ(vmErr.code, "CTX_PHYSICAL_AXIS_LOCKED");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}

TEST_F(ErrorTranslatorTest, ContextRejection_LogicalAxisUnavailableWhenDecoupled) {
    auto vmErr = translate(UseCaseError{ContextRejection::LogicalAxisUnavailableWhenDecoupled});
    EXPECT_EQ(vmErr.code, "CTX_LOGICAL_AXIS_UNAVAILABLE");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}

TEST_F(ErrorTranslatorTest, ContextRejection_GantryNotSynchronized) {
    auto vmErr = translate(UseCaseError{ContextRejection::GantryNotSynchronized});
    EXPECT_EQ(vmErr.code, "CTX_GANTRY_NOT_SYNCED");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, ContextRejection_AxisNotRegistered) {
    auto vmErr = translate(UseCaseError{ContextRejection::AxisNotRegistered});
    EXPECT_EQ(vmErr.code, "CTX_AXIS_NOT_REGISTERED");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, ContextRejection_SystemSafetyLocked) {
    auto vmErr = translate(UseCaseError{ContextRejection::SystemSafetyLocked});
    EXPECT_EQ(vmErr.code, "CTX_SAFETY_LOCKED");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, ContextRejection_DriverNotReady) {
    auto vmErr = translate(UseCaseError{ContextRejection::DriverNotReady});
    EXPECT_EQ(vmErr.code, "CTX_DRIVER_NOT_READY");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, ContextRejection_None) {
    auto vmErr = translate(UseCaseError{ContextRejection::None});
    EXPECT_EQ(vmErr.code, "CTX_UNKNOWN_ERROR");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

// ============================================================================
// 4. CommunicationResult（通讯层）
// ============================================================================

TEST_F(ErrorTranslatorTest, CommunicationResult_NetworkError) {
    CommunicationResult commResult;
    commResult.status = CommunicationResult::Status::NetworkError;
    commResult.diagnostic = "ECONNREFUSED: 192.168.1.100:502";
    auto vmErr = translate(UseCaseError{commResult});
    EXPECT_EQ(vmErr.code, "COMM_NETWORK_ERROR");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
    EXPECT_EQ(vmErr.debugMessage, "ECONNREFUSED: 192.168.1.100:502");
}

TEST_F(ErrorTranslatorTest, CommunicationResult_Timeout) {
    CommunicationResult commResult;
    commResult.status = CommunicationResult::Status::Timeout;
    commResult.diagnostic = "read timeout 2000ms";
    auto vmErr = translate(UseCaseError{commResult});
    EXPECT_EQ(vmErr.code, "COMM_TIMEOUT");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, CommunicationResult_Busy) {
    CommunicationResult commResult;
    commResult.status = CommunicationResult::Status::Busy;
    commResult.diagnostic = "PLC busy";
    auto vmErr = translate(UseCaseError{commResult});
    EXPECT_EQ(vmErr.code, "COMM_PLC_BUSY");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}

TEST_F(ErrorTranslatorTest, CommunicationResult_ProtocolError) {
    CommunicationResult commResult;
    commResult.status = CommunicationResult::Status::ProtocolError;
    commResult.exceptionCode = 0x02;
    commResult.diagnostic = "Illegal Data Address";
    auto vmErr = translate(UseCaseError{commResult});
    EXPECT_EQ(vmErr.code, "COMM_PROTOCOL_ERROR");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
    EXPECT_NE(vmErr.debugMessage.find("0x2"), std::string::npos);
}

TEST_F(ErrorTranslatorTest, CommunicationResult_InvalidResponse) {
    CommunicationResult commResult;
    commResult.status = CommunicationResult::Status::InvalidResponse;
    commResult.diagnostic = "CRC mismatch";
    auto vmErr = translate(UseCaseError{commResult});
    EXPECT_EQ(vmErr.code, "COMM_INVALID_RESPONSE");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, CommunicationResult_Disconnected) {
    CommunicationResult commResult;
    commResult.status = CommunicationResult::Status::Disconnected;
    commResult.diagnostic = "Not connected";
    auto vmErr = translate(UseCaseError{commResult});
    EXPECT_EQ(vmErr.code, "COMM_DISCONNECTED");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, CommunicationResult_SentShouldFallThrough) {
    CommunicationResult commResult;
    commResult.status = CommunicationResult::Status::Sent;
    commResult.diagnostic = "should not appear";
    auto vmErr = translate(UseCaseError{commResult});
    EXPECT_EQ(vmErr.code, "COMM_UNKNOWN");
}

// ============================================================================
// 5. GantryRejection（龙门联动层）
// ============================================================================

TEST_F(ErrorTranslatorTest, GantryRejection_None) {
    auto vmErr = translate(UseCaseError{GantryRejection::None});
    EXPECT_EQ(vmErr.code, "GANTRY_NONE");
}

TEST_F(ErrorTranslatorTest, GantryRejection_PositionToleranceExceeded) {
    auto vmErr = translate(UseCaseError{GantryRejection::PositionToleranceExceeded});
    EXPECT_EQ(vmErr.code, "GANTRY_POS_TOLERANCE_EXCEEDED");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, GantryRejection_X1NotEnabled) {
    auto vmErr = translate(UseCaseError{GantryRejection::X1NotEnabled});
    EXPECT_EQ(vmErr.code, "GANTRY_X1_NOT_ENABLED");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}

TEST_F(ErrorTranslatorTest, GantryRejection_X2NotEnabled) {
    auto vmErr = translate(UseCaseError{GantryRejection::X2NotEnabled});
    EXPECT_EQ(vmErr.code, "GANTRY_X2_NOT_ENABLED");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}

TEST_F(ErrorTranslatorTest, GantryRejection_X1NotStationary) {
    auto vmErr = translate(UseCaseError{GantryRejection::X1NotStationary});
    EXPECT_EQ(vmErr.code, "GANTRY_X1_NOT_STATIONARY");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}

TEST_F(ErrorTranslatorTest, GantryRejection_X2NotStationary) {
    auto vmErr = translate(UseCaseError{GantryRejection::X2NotStationary});
    EXPECT_EQ(vmErr.code, "GANTRY_X2_NOT_STATIONARY");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}

TEST_F(ErrorTranslatorTest, GantryRejection_StateConflict) {
    auto vmErr = translate(UseCaseError{GantryRejection::StateConflict});
    EXPECT_EQ(vmErr.code, "GANTRY_STATE_CONFLICT");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}

TEST_F(ErrorTranslatorTest, GantryRejection_NotSynchronized) {
    auto vmErr = translate(UseCaseError{GantryRejection::NotSynchronized});
    EXPECT_EQ(vmErr.code, "GANTRY_NOT_SYNCED");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, GantryRejection_UnknownError) {
    auto vmErr = translate(UseCaseError{GantryRejection::UnknownError});
    EXPECT_EQ(vmErr.code, "GANTRY_UNKNOWN");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

// ============================================================================
// 6. SafetyRejection（安全域急停层）
// ============================================================================

TEST_F(ErrorTranslatorTest, SafetyRejection_None) {
    auto vmErr = translate(UseCaseError{SafetyRejection::None});
    EXPECT_EQ(vmErr.code, "SAFETY_NONE");
}

TEST_F(ErrorTranslatorTest, SafetyRejection_SystemSafetyLocked) {
    auto vmErr = translate(UseCaseError{SafetyRejection::SystemSafetyLocked});
    EXPECT_EQ(vmErr.code, "SAFETY_SYSTEM_LOCKED");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, SafetyRejection_AlreadyInState) {
    auto vmErr = translate(UseCaseError{SafetyRejection::AlreadyInState});
    EXPECT_EQ(vmErr.code, "SAFETY_ALREADY_IN_STATE");
    EXPECT_EQ(vmErr.category, ErrorCategory::Silent);
}

TEST_F(ErrorTranslatorTest, SafetyRejection_InvalidStateTransition) {
    auto vmErr = translate(UseCaseError{SafetyRejection::InvalidStateTransition});
    EXPECT_EQ(vmErr.code, "SAFETY_INVALID_TRANSITION");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, SafetyRejection_NotSynchronized) {
    auto vmErr = translate(UseCaseError{SafetyRejection::NotSynchronized});
    EXPECT_EQ(vmErr.code, "SAFETY_NOT_SYNCED");
    EXPECT_EQ(vmErr.category, ErrorCategory::Modal);
}

TEST_F(ErrorTranslatorTest, SafetyRejection_NotEmergencyStopped) {
    auto vmErr = translate(UseCaseError{SafetyRejection::NotEmergencyStopped});
    EXPECT_EQ(vmErr.code, "SAFETY_NOT_EMERGENCY_STOPPED");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}
