#include "ErrorTranslator.h"
#include <type_traits>

ViewModelError translate(const UseCaseError& err) {
    return std::visit([](const auto& e) -> ViewModelError {
        using T = std::decay_t<decltype(e)>;

        // =============================================
        // 成功（不应被翻译，调用方先检查 monostate）
        // =============================================
        if constexpr (std::is_same_v<T, std::monostate>) {
            return {"", "", "", ErrorCategory::Silent};
        }

        // =============================================
        // Axis 领域层错误
        // =============================================
        else if constexpr (std::is_same_v<T, RejectionReason>) {
            switch (e) {
            case RejectionReason::InvalidState:
                return {"AXIS_INVALID_STATE", "轴状态无效，操作被拒绝",
                        "Axis is in an invalid state for this operation",
                        ErrorCategory::Inline};
            case RejectionReason::AlreadyMoving:
                return {"AXIS_ALREADY_MOVING", "轴正在运动中",
                        "Cannot execute operation while axis is moving",
                        ErrorCategory::Inline};
            case RejectionReason::TargetOutOfPositiveLimit:
                return {"AXIS_TARGET_OUT_OF_POS_LIMIT", "目标位置超出正向限位",
                        "Target exceeds positive soft limit",
                        ErrorCategory::Inline};
            case RejectionReason::TargetOutOfNegativeLimit:
                return {"AXIS_TARGET_OUT_OF_NEG_LIMIT", "目标位置超出负向限位",
                        "Target exceeds negative soft limit",
                        ErrorCategory::Inline};
            case RejectionReason::AtPositiveLimit:
                return {"AXIS_AT_POSITIVE_LIMIT", "轴已到达正向限位",
                        "Axis is at positive limit, forward motion blocked",
                        ErrorCategory::Inline};
            case RejectionReason::AtNegativeLimit:
                return {"AXIS_AT_NEGATIVE_LIMIT", "轴已到达负向限位",
                        "Axis is at negative limit, backward motion blocked",
                        ErrorCategory::Inline};
            case RejectionReason::InvalidArgument:
                return {"AXIS_INVALID_ARGUMENT", "参数无效",
                        "Invalid argument provided to axis operation",
                        ErrorCategory::Inline};
            case RejectionReason::UnknownError:
            case RejectionReason::None:
            default:
                return {"AXIS_UNKNOWN_ERROR", "轴发生未知错误",
                        "Unknown axis error",
                        ErrorCategory::Modal};
            }
        }

        // =============================================
        // SystemManager / SystemContext 层错误
        // =============================================
        else if constexpr (std::is_same_v<T, ContextRejection>) {
            switch (e) {
            case ContextRejection::GroupNotFound:
                return {"CTX_GROUP_NOT_FOUND", "设备分组不存在",
                        "System group not found",
                        ErrorCategory::Modal};
            case ContextRejection::GroupAlreadyExists:
                return {"CTX_GROUP_ALREADY_EXISTS", "设备分组已存在",
                        "System group already exists",
                        ErrorCategory::Modal};
            case ContextRejection::GroupNameInvalid:
                return {"CTX_GROUP_NAME_INVALID", "设备分组名称无效",
                        "System group name is invalid",
                        ErrorCategory::Modal};
            case ContextRejection::PhysicalAxisLockedByGantry:
                return {"CTX_PHYSICAL_AXIS_LOCKED", "物理轴被龙门联动锁定",
                        "Physical axis is locked by gantry coupling",
                        ErrorCategory::Inline};
            case ContextRejection::LogicalAxisUnavailableWhenDecoupled:
                return {"CTX_LOGICAL_AXIS_UNAVAILABLE", "龙门解耦时逻辑轴不可用",
                        "Logical axis unavailable when gantry is decoupled",
                        ErrorCategory::Inline};
            case ContextRejection::GantryNotSynchronized:
                return {"CTX_GANTRY_NOT_SYNCED", "龙门状态未同步",
                        "Gantry state not yet synchronized with PLC",
                        ErrorCategory::Modal};
            case ContextRejection::AxisNotRegistered:
                return {"CTX_AXIS_NOT_REGISTERED", "轴未注册",
                        "Axis not registered in system context",
                        ErrorCategory::Modal};
            case ContextRejection::SystemSafetyLocked:
                return {"CTX_SAFETY_LOCKED", "系统安全锁定中",
                        "System is in safety lock state (emergency stop active)",
                        ErrorCategory::Modal};
            case ContextRejection::DriverNotReady:
                return {"CTX_DRIVER_NOT_READY", "驱动未就绪",
                        "System driver is not ready",
                        ErrorCategory::Modal};
            case ContextRejection::None:
            default:
                return {"CTX_UNKNOWN_ERROR", "系统上下文未知错误",
                        "Unknown context error",
                        ErrorCategory::Modal};
            }
        }

        // =============================================
        // 通讯层错误
        // =============================================
        else if constexpr (std::is_same_v<T, CommunicationResult>) {
            switch (e.status) {
            case CommunicationResult::Status::NetworkError:
                return {"COMM_NETWORK_ERROR", "网络通讯故障",
                        e.diagnostic,
                        ErrorCategory::Modal};
            case CommunicationResult::Status::Timeout:
                return {"COMM_TIMEOUT", "通讯超时",
                        e.diagnostic,
                        ErrorCategory::Modal};
            case CommunicationResult::Status::Busy:
                return {"COMM_PLC_BUSY", "PLC 忙，请稍后重试",
                        e.diagnostic,
                        ErrorCategory::Inline};
            case CommunicationResult::Status::ProtocolError:
                return {"COMM_PROTOCOL_ERROR", "Modbus 协议错误",
                        "Exception code: 0x" + std::to_string(e.exceptionCode) + " " + e.diagnostic,
                        ErrorCategory::Modal};
            case CommunicationResult::Status::InvalidResponse:
                return {"COMM_INVALID_RESPONSE", "PLC 返回数据异常",
                        e.diagnostic,
                        ErrorCategory::Modal};
            case CommunicationResult::Status::Disconnected:
                return {"COMM_DISCONNECTED", "设备未连接",
                        e.diagnostic,
                        ErrorCategory::Modal};
            case CommunicationResult::Status::Sent:
            default:
                return {"COMM_UNKNOWN", "通讯未知错误",
                        e.diagnostic,
                        ErrorCategory::Modal};
            }
        }

        // =============================================
        // 龙门联动层错误
        // =============================================
        else if constexpr (std::is_same_v<T, GantryRejection>) {
            switch (e) {
            case GantryRejection::None:
                return {"GANTRY_NONE", "龙门操作未知错误",
                        "",
                        ErrorCategory::Silent};
            case GantryRejection::PositionToleranceExceeded:
                return {"GANTRY_POS_TOLERANCE_EXCEEDED", "龙门联动位置超差",
                        "Position tolerance exceeded during gantry coupling",
                        ErrorCategory::Modal};
            case GantryRejection::X1NotEnabled:
                return {"GANTRY_X1_NOT_ENABLED", "龙门 X1 轴未使能，请先上电",
                        "Gantry X1 motor not enabled before coupling",
                        ErrorCategory::Inline};
            case GantryRejection::X2NotEnabled:
                return {"GANTRY_X2_NOT_ENABLED", "龙门 X2 轴未使能，请先上电",
                        "Gantry X2 motor not enabled before coupling",
                        ErrorCategory::Inline};
            case GantryRejection::X1NotStationary:
                return {"GANTRY_X1_NOT_STATIONARY", "龙门 X1 轴未静止",
                        "Gantry X1 is not stationary before coupling",
                        ErrorCategory::Inline};
            case GantryRejection::X2NotStationary:
                return {"GANTRY_X2_NOT_STATIONARY", "龙门 X2 轴未静止",
                        "Gantry X2 is not stationary before coupling",
                        ErrorCategory::Inline};
            case GantryRejection::StateConflict:
                return {"GANTRY_STATE_CONFLICT", "龙门状态冲突",
                        "Gantry internal state conflict (e.g. already in requested state)",
                        ErrorCategory::Inline};
            case GantryRejection::NotSynchronized:
                return {"GANTRY_NOT_SYNCED", "龙门状态未同步",
                        "Gantry state not yet synchronized with PLC",
                        ErrorCategory::Modal};
            case GantryRejection::UnknownError:
            default:
                return {"GANTRY_UNKNOWN", "龙门未知错误",
                        "",
                        ErrorCategory::Modal};
            }
        }

        // =============================================
        // Policy 策略层超时错误
        // =============================================
        else if constexpr (std::is_same_v<T, ErrTimeout>) {
            return {"POLICY_TIMEOUT",
                    "策略步 " + e.step + " 超时（" + std::to_string(e.timeoutSec) + "s）",
                    "Policy step '" + e.step + "' timed out after "
                        + std::to_string(e.timeoutSec) + " seconds",
                    ErrorCategory::Modal};
        }

        // =============================================
        // 安全域急停层错误
        // =============================================
        else if constexpr (std::is_same_v<T, SafetyRejection>) {
            switch (e) {
            case SafetyRejection::None:
                return {"SAFETY_NONE", "安全域未知错误",
                        "",
                        ErrorCategory::Silent};
            case SafetyRejection::SystemSafetyLocked:
                return {"SAFETY_SYSTEM_LOCKED", "系统安全锁定中",
                        "System is in safety lock state (emergency stop active)",
                        ErrorCategory::Modal};
            case SafetyRejection::AlreadyInState:
                return {"SAFETY_ALREADY_IN_STATE", "操作已在目标状态",
                        "System is already in the requested safety state",
                        ErrorCategory::Silent};
            case SafetyRejection::InvalidStateTransition:
                return {"SAFETY_INVALID_TRANSITION", "安全状态转换非法",
                        "Safety state transition is invalid",
                        ErrorCategory::Modal};
            case SafetyRejection::NotSynchronized:
                return {"SAFETY_NOT_SYNCED", "安全状态未同步",
                        "Safety state not yet synchronized with PLC",
                        ErrorCategory::Modal};
            case SafetyRejection::NotEmergencyStopped:
                return {"SAFETY_NOT_EMERGENCY_STOPPED", "系统未处于急停状态",
                        "Cannot release emergency stop when system is not emergency stopped",
                        ErrorCategory::Inline};
            default:
                return {"SAFETY_UNKNOWN", "安全域未知错误",
                        "",
                        ErrorCategory::Modal};
            }
        }
    }, err);
}
