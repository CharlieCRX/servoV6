#pragma once

#include <string>
#include <optional>
#include <thread>
#include <chrono>
#include <QCoreApplication>
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
#include "infrastructure/ISystemDriver.h"
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

        // ── 步骤 5：Axis 获取 ──
        // 控制类操作（cmd=0,1,3,5）使用 tryGetAxis（含安全拦截），
        // 查询类操作（cmd=2,4）使用 tryReadAxis（跳过安全拦截），
        // 未知 cmd 安全回退到 tryReadAxis。
        Axis* axis = nullptr;
        ContextRejection ctxReason = ContextRejection::None;
        bool isControlCmd = (cmdInt == 0 || cmdInt == 1 || cmdInt == 3 || cmdInt == 5);
        bool axisOk = isControlCmd
            ? group->tryGetAxis(axisId, axis, ctxReason)
            : group->tryReadAxis(axisId, axis, ctxReason);
        if (!axisOk) {
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

    /// cmd=0: 基于相对零点的绝对位置移动（文档 §3.4.1）
    std::string handleMoveToRelTarget(const QJsonObject& req, Axis& axis, SystemContext& group) {
        // 必填字段校验
        if (!req.contains(QString::fromUtf8(UdpField::TARGET))) {
            return UdpResponseBuilder::buildError(req,
                "missing required field 'target' for cmd=0");
        }
        double target = req[QString::fromUtf8(UdpField::TARGET)].toDouble();

        // 1. 计算最终绝对目标位置
        //    finalAbsTarget = target + relZeroAbsPos
        double relZeroAbsPos = axis.relativeZeroAbsolutePosition();
        double finalAbsTarget = target + relZeroAbsPos;

        // 2. 通过 Axis 实体写入 ABS_TARGET 到 PLC
        if (!axis.setAbsTarget(finalAbsTarget)) {
            RejectionReason reason = axis.lastRejection();
            std::string errMsg = "setAbsTarget(" + std::to_string(finalAbsTarget)
                + ") rejected: " + rejectionReasonToString(reason);
            LOG_WARN(LogLayer::APP, "UdpDispatcher",
                     "[MOVE_TO_REL_TARGET] " + errMsg);
            return UdpResponseBuilder::buildError(req, errMsg);
        }

        // 3. 消费 pending command → 将 SetAbsTargetCommand 下发到 PLC
        if (auto commErr = consumePendingCommand(axis, group, "writing target")) {
            return UdpResponseBuilder::buildError(req, *commErr);
        }

        // 4. 触发绝对位置移动（使用 AbsMovePolicy）
        std::string groupName = req[QString::fromUtf8(UdpField::GROUP)].toString().toStdString();
        AbsMovePolicy absPolicy(m_manager, groupName);
        absPolicy.startAbs(AxisId::R);

        // 5. 驱动 Policy 状态机直到完成或出错（阻塞等待模式）
        //    ★ 先 pollFeedback 刷新 PLC 状态，再用最新反馈推进 Policy
        //    避免 tick 后立即 poll 读到的仍是"命令已送达但未生效"的旧状态，
        //    导致 Policy 误判"运动瞬间完成"。
        while (absPolicy.currentStep() != AbsMovePolicy::Step::Done &&
               absPolicy.currentStep() != AbsMovePolicy::Step::Error) {
            if (auto* drv = group.driver()) {
                drv->pollFeedback(group);
            }
            absPolicy.tick();
            // 匹配 PLC Modbus 扫描周期（~10ms），避免空转占用 CPU
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            // 让 Qt 处理 UI 事件，避免界面卡死
            QCoreApplication::processEvents();
        }

        // 6. 获取结果
        if (absPolicy.hasError()) {
            auto err = absPolicy.lastError();
            std::string errMsg = "AbsMovePolicy failed: " + useCaseErrorToString(err);
            LOG_WARN(LogLayer::APP, "UdpDispatcher", "[MOVE_TO_REL_TARGET] " + errMsg);
            return UdpResponseBuilder::buildError(req, errMsg);
        }

        // 7. 构建成功回复
        double currPos = axis.currentRelativePosition();
        LOG_INFO(LogLayer::APP, "UdpDispatcher",
                 "[MOVE_TO_REL_TARGET] SUCCESS: target=" + std::to_string(target)
                 + " finalAbs=" + std::to_string(finalAbsTarget)
                 + " curr=" + std::to_string(currPos));
        return UdpResponseBuilder::buildSuccess(req, {{std::string(UdpField::CURR), currPos}});
    }

    /// cmd=1: 相对偏移移动（文档 §3.4.2）
    std::string handleMoveOffset(const QJsonObject& req, Axis& axis, SystemContext& group) {
        // 必填字段校验
        if (!req.contains(QString::fromUtf8(UdpField::OFFSET))) {
            return UdpResponseBuilder::buildError(req,
                "missing required field 'offset' for cmd=1");
        }
        double offset = req[QString::fromUtf8(UdpField::OFFSET)].toDouble();

        // 1. 通过 Axis 实体设置相对移动距离
        if (!axis.setRelTarget(offset)) {
            RejectionReason reason = axis.lastRejection();
            std::string errMsg = "setRelTarget(" + std::to_string(offset)
                + ") rejected: " + rejectionReasonToString(reason);
            LOG_WARN(LogLayer::APP, "UdpDispatcher", "[MOVE_OFFSET] " + errMsg);
            return UdpResponseBuilder::buildError(req, errMsg);
        }

        // 2. 消费 pending command → 将 SetRelTargetCommand 下发到 PLC
        if (auto commErr = consumePendingCommand(axis, group, "writing rel target")) {
            return UdpResponseBuilder::buildError(req, *commErr);
        }

        // 3. 触发相对位置移动（使用 RelMovePolicy）
        std::string groupName = req[QString::fromUtf8(UdpField::GROUP)].toString().toStdString();
        RelMovePolicy relPolicy(m_manager, groupName);
        relPolicy.startRel(AxisId::R);

        // 4. 驱动 Policy 直到完成
        //    ★ 先 pollFeedback 刷新 PLC 状态，再用最新反馈推进 Policy
        while (relPolicy.currentStep() != RelMovePolicy::Step::Done &&
               relPolicy.currentStep() != RelMovePolicy::Step::Error) {
            if (auto* drv = group.driver()) {
                drv->pollFeedback(group);
            }
            relPolicy.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            // 让 Qt 处理 UI 事件，避免界面卡死
            QCoreApplication::processEvents();
        }

        // 5. 结果处理
        if (relPolicy.hasError()) {
            auto err = relPolicy.lastError();
            std::string errMsg = "RelMovePolicy failed: " + useCaseErrorToString(err);
            LOG_WARN(LogLayer::APP, "UdpDispatcher", "[MOVE_OFFSET] " + errMsg);
            return UdpResponseBuilder::buildError(req, errMsg);
        }

        double currPos = axis.currentRelativePosition();
        LOG_INFO(LogLayer::APP, "UdpDispatcher",
                 "[MOVE_OFFSET] SUCCESS: offset=" + std::to_string(offset)
                 + " curr=" + std::to_string(currPos));
        return UdpResponseBuilder::buildSuccess(req, {{std::string(UdpField::CURR), currPos}});
    }

    /// cmd=2: 获取当前相对位置
    std::string handleGetRelPosition(const QJsonObject& req, Axis& axis) {
        // ★ 查询操作：阶段 1 即可实现（文档 §3.4.3）
        double currPos = axis.currentRelativePosition();
        return UdpResponseBuilder::buildSuccess(req, {{std::string(UdpField::CURR), currPos}});
    }

    /// cmd=3: 设置位置移动速度（文档 §3.4.4）
    std::string handleSetMoveSpeed(const QJsonObject& req, Axis& axis, SystemContext& group) {
        // 必填字段校验
        if (!req.contains(QString::fromUtf8(UdpField::SPEED))) {
            return UdpResponseBuilder::buildError(req,
                "missing required field 'speed' for cmd=3");
        }
        double speed = req[QString::fromUtf8(UdpField::SPEED)].toDouble();

        // 1. 通过 Axis 实体设置速度
        if (!axis.setMoveVelocity(speed)) {
            std::string errMsg = "setMoveVelocity(" + std::to_string(speed) + ") rejected";
            LOG_WARN(LogLayer::APP, "UdpDispatcher", "[SET_MOVE_SPEED] " + errMsg);
            return UdpResponseBuilder::buildError(req, errMsg);
        }

        // 2. 消费 pending command → 将 SetMoveVelocityCommand 下发到 PLC
        if (auto commErr = consumePendingCommand(axis, group, "setting speed")) {
            return UdpResponseBuilder::buildError(req, *commErr);
        }

        LOG_INFO(LogLayer::APP, "UdpDispatcher",
                 "[SET_MOVE_SPEED] SUCCESS: speed=" + std::to_string(speed));
        return UdpResponseBuilder::buildSuccess(req, {});
    }

    /// cmd=4: 获取位置移动速度
    std::string handleGetMoveSpeed(const QJsonObject& req, Axis& axis) {
        // ★ 查询操作：阶段 1 即可实现（文档 §3.4.5）
        double speed = axis.getMoveVelocity();
        return UdpResponseBuilder::buildSuccess(req, {{std::string(UdpField::SPEED), speed}});
    }

    /// cmd=5: 设置相对零点（文档 §3.4.6）
    std::string handleSetRelZero(const QJsonObject& req, Axis& axis, SystemContext& group) {
        // 1. 调用 setRelativeZero() 设置当前绝对位置为相对零点
        if (!axis.setRelativeZero()) {
            std::string errMsg = "setRelativeZero rejected: "
                + rejectionReasonToString(axis.lastRejection());
            LOG_WARN(LogLayer::APP, "UdpDispatcher", "[SET_REL_ZERO] " + errMsg);
            return UdpResponseBuilder::buildError(req, errMsg);
        }

        // 2. 消费 pending command → 将 SetRelativeZeroCommand 下发到 PLC
        if (auto commErr = consumePendingCommand(axis, group, "setting rel zero")) {
            return UdpResponseBuilder::buildError(req, *commErr);
        }

        LOG_INFO(LogLayer::APP, "UdpDispatcher", "[SET_REL_ZERO] SUCCESS");
        return UdpResponseBuilder::buildSuccess(req, {});
    }

    // ═══════════════════════════════════════════════════════════
    // 私有辅助方法
    // ═══════════════════════════════════════════════════════════

    /// @brief 消费 Axis pending command 并通过 driver 下发到 PLC
    /// @param axis 目标轴
    /// @param group 目标分组上下文
    /// @param context 操作描述（用于错误消息）
    /// @return 通讯错误描述（若成功则返回 std::nullopt）
    std::optional<std::string> consumePendingCommand(Axis& axis, SystemContext& group,
                                                      const std::string& context) {
        if (!axis.hasPendingCommand()) return std::nullopt;

        if (auto* drv = group.driver()) {
            auto commResult = drv->send(AxisCommandWithId{AxisId::R, axis.getPendingCommand()});
            if (!commResult.ok()) {
                return "PLC communication failed when " + context
                     + ": " + commResult.diagnostic;
            }
        }
        return std::nullopt;
    }
};