#pragma once

#include <string>
#include <QJsonDocument>
#include <QJsonObject>

#include "application/udp/UdpProtocol.h"
#include "application/udp/UdpResponseBuilder.h"
#include "application/SystemManager.h"
#include "application/policy/AbsMovePolicy.h"
#include "application/policy/RelMovePolicy.h"
#include "domain/entity/Axis.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/ContextRejection.h"
#include "infrastructure/logger/Logger.h"

// ═══════════════════════════════════════════════════════════════════
// UDP 命令分发器 —— 解析、校验、路由、执行 UDP 命令
// 参考: docs/architecture/UDP通讯层设计文档.md §3.3.3, §3.4
//
// ★ 阶段 1：提供前置校验骨架 + cmd 路由框架
//    阶段 2：补充各 cmd 的具体业务逻辑
// ═══════════════════════════════════════════════════════════════════

// ============================================================
// 辅助函数：RejectionReason → 字符串
// ============================================================
inline std::string rejectionReasonToString(RejectionReason r) {
    switch (r) {
        case RejectionReason::None:                     return "None";
        case RejectionReason::InvalidState:             return "InvalidState";
        case RejectionReason::AlreadyMoving:            return "AlreadyMoving";
        case RejectionReason::TargetOutOfPositiveLimit:  return "TargetOutOfPositiveLimit";
        case RejectionReason::TargetOutOfNegativeLimit:  return "TargetOutOfNegativeLimit";
        case RejectionReason::AtPositiveLimit:           return "AtPositiveLimit";
        case RejectionReason::AtNegativeLimit:           return "AtNegativeLimit";
        case RejectionReason::UnknownError:              return "UnknownError";
        case RejectionReason::InvalidArgument:           return "InvalidArgument";
    }
    return "UnknownRejection";
}

// ============================================================
// 辅助函数：UseCaseError → 字符串
// ============================================================
inline std::string useCaseErrorToString(const UseCaseError& e) {
    return std::visit([](const auto& val) -> std::string {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return "Success";
        } else if constexpr (std::is_same_v<T, ContextRejection>) {
            return std::string("ContextRejection::") + contextRejectionToString(val);
        } else if constexpr (std::is_same_v<T, RejectionReason>) {
            return rejectionReasonToString(val);
        } else if constexpr (std::is_same_v<T, CommunicationResult>) {
            return "CommunicationFailure: " + val.diagnostic;
        } else if constexpr (std::is_same_v<T, ErrTimeout>) {
            return "Timeout in " + val.step + " (" + std::to_string(val.timeoutSec) + "s)";
        } else {
            return "UnknownError";
        }
    }, e);
}

// ============================================================
// UdpCommandDispatcher
// ============================================================
class UdpCommandDispatcher {
public:
    explicit UdpCommandDispatcher(SystemManager& manager)
        : m_manager(manager) {}

    /// @brief 处理一条 UDP 命令并返回 JSON 回复
    /// @param rawJson 原始 JSON 字符串
    /// @return JSON 格式的回复字符串
    std::string dispatch(const std::string& rawJson) {
        // ── 步骤 1：JSON 解析 ──
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(rawJson), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            LOG_WARN(LogLayer::APP, "UdpDispatcher",
                     "JSON parse failed: " + parseError.errorString().toStdString());
            // 无法解析原始请求字段，返回最小错误回复
            return UdpResponseBuilder::buildRawError(0, 0, "invalid JSON format");
        }
        QJsonObject req = doc.object();

        // ── 步骤 2：必填字段检查 ──
        if (!req.contains(QString::fromUtf8(UdpField::CMD))) {
            LOG_WARN(LogLayer::APP, "UdpDispatcher", "missing required field 'cmd'");
            return UdpResponseBuilder::buildError(req, "missing required field 'cmd'");
        }
        if (!req.contains(QString::fromUtf8(UdpField::MOTOR))) {
            LOG_WARN(LogLayer::APP, "UdpDispatcher", "missing required field 'motor'");
            return UdpResponseBuilder::buildError(req, "missing required field 'motor'");
        }
        if (!req.contains(QString::fromUtf8(UdpField::GROUP))) {
            LOG_WARN(LogLayer::APP, "UdpDispatcher", "missing required field 'group'");
            return UdpResponseBuilder::buildError(req, "missing required field 'group'");
        }

        int motor = req[QString::fromUtf8(UdpField::MOTOR)].toInt();
        int cmdInt = req[QString::fromUtf8(UdpField::CMD)].toInt();
        std::string groupName = req[QString::fromUtf8(UdpField::GROUP)].toString().toStdString();

        LOG_INFO(LogLayer::APP, "UdpDispatcher",
                 "dispatch: cmd=" + std::to_string(cmdInt)
                 + " motor=" + std::to_string(motor)
                 + " group=" + groupName);

        // ── 步骤 3：motor 校验（仅 R 轴 motor=2 通过）──
        AxisId axisId;
        if (!motorToAxisId(motor, axisId)) {
            std::string errMsg = "motor " + std::to_string(motor) + " not supported, only R axis (motor=" + std::to_string(R_MOTOR_ID) + ")";
            LOG_WARN(LogLayer::APP, "UdpDispatcher", errMsg);
            return UdpResponseBuilder::buildError(req, errMsg);
        }

        // ── 步骤 4：group 校验 ──
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!m_manager.tryGetGroup(groupName, group, mgrReason)) {
            std::string errMsg = "group '" + groupName + "' not found: " + contextRejectionToString(mgrReason);
            LOG_WARN(LogLayer::APP, "UdpDispatcher", errMsg);
            return UdpResponseBuilder::buildError(req, errMsg);
        }

        // ── 步骤 5：Axis 获取（使用 tryReadAxis 绕过安全锁定，允许查询类操作在急停时仍可读取）──
        Axis* axis = nullptr;
        ContextRejection ctxReason = ContextRejection::None;
        // 对于控制类操作（cmd=0,1,3,5）使用 tryGetAxis（含安全拦截），
        // 对于查询类操作（cmd=2,4）使用 tryReadAxis（跳过安全拦截）。
        // 为简化阶段 1 骨架，统一先使用 tryReadAxis，阶段 2 细化。
        if (!group->tryReadAxis(axisId, axis, ctxReason)) {
            std::string errMsg = "R axis not available: " + std::string(contextRejectionToString(ctxReason));
            LOG_WARN(LogLayer::APP, "UdpDispatcher", errMsg);
            return UdpResponseBuilder::buildError(req, errMsg);
        }

        // ── 步骤 6：按 cmd 路由 ──
        UdpCmd cmd = static_cast<UdpCmd>(cmdInt);
        switch (cmd) {
            case UdpCmd::MOVE_TO_REL_TARGET: // cmd=0
                return handleMoveToRelTarget(req, *axis, *group);

            case UdpCmd::MOVE_OFFSET:        // cmd=1
                return handleMoveOffset(req, *axis, *group);

            case UdpCmd::GET_REL_POSITION:   // cmd=2
                return handleGetRelPosition(req, *axis);

            case UdpCmd::SET_MOVE_SPEED:     // cmd=3
                return handleSetMoveSpeed(req, *axis, *group);

            case UdpCmd::GET_MOVE_SPEED:     // cmd=4
                return handleGetMoveSpeed(req, *axis);

            case UdpCmd::SET_REL_ZERO:       // cmd=5
                return handleSetRelZero(req, *axis, *group);

            default: {
                std::string errMsg = "unknown cmd: " + std::to_string(cmdInt);
                LOG_WARN(LogLayer::APP, "UdpDispatcher", errMsg);
                return UdpResponseBuilder::buildError(req, errMsg);
            }
        }
    }

private:
    SystemManager& m_manager;

    // ═══════════════════════════════════════════════════════════
    // 阶段 2 将实现以下六个处理函数的完整业务逻辑。
    // 阶段 1 提供空骨架，返回"命令已识别但未实现"的错误。
    // ═══════════════════════════════════════════════════════════

    /// cmd=0: 基于相对零点的绝对位置移动
    std::string handleMoveToRelTarget(const QJsonObject& req, Axis& axis, SystemContext& group) {
        // 阶段 2 实现（文档 §3.4.1）
        return UdpResponseBuilder::buildError(req, "cmd=0 (MOVE_TO_REL_TARGET) not implemented in stage 1");
    }

    /// cmd=1: 相对偏移移动
    std::string handleMoveOffset(const QJsonObject& req, Axis& axis, SystemContext& group) {
        // 阶段 2 实现（文档 §3.4.2）
        return UdpResponseBuilder::buildError(req, "cmd=1 (MOVE_OFFSET) not implemented in stage 1");
    }

    /// cmd=2: 获取当前相对位置
    std::string handleGetRelPosition(const QJsonObject& req, Axis& axis) {
        // ★ 查询操作：阶段 1 即可实现（文档 §3.4.3）
        double currPos = axis.currentRelativePosition();
        return UdpResponseBuilder::buildSuccess(req, {{std::string(UdpField::CURR), currPos}});
    }

    /// cmd=3: 设置位置移动速度
    std::string handleSetMoveSpeed(const QJsonObject& req, Axis& axis, SystemContext& group) {
        // 阶段 2 实现（文档 §3.4.4）
        return UdpResponseBuilder::buildError(req, "cmd=3 (SET_MOVE_SPEED) not implemented in stage 1");
    }

    /// cmd=4: 获取位置移动速度
    std::string handleGetMoveSpeed(const QJsonObject& req, Axis& axis) {
        // ★ 查询操作：阶段 1 即可实现（文档 §3.4.5）
        double speed = axis.getMoveVelocity();
        return UdpResponseBuilder::buildSuccess(req, {{std::string(UdpField::SPEED), speed}});
    }

    /// cmd=5: 设置相对零点
    std::string handleSetRelZero(const QJsonObject& req, Axis& axis, SystemContext& group) {
        // 阶段 2 实现（文档 §3.4.6）
        return UdpResponseBuilder::buildError(req, "cmd=5 (SET_REL_ZERO) not implemented in stage 1");
    }
};