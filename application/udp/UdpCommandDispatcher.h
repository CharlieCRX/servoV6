#pragma once

#include <string>
#include <optional>
#include <utility>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "application/udp/UdpProtocol.h"
#include "application/udp/UdpResponseBuilder.h"
#include "application_vnext/control/MotionControlService.h"
#include "infrastructure/logger/Logger.h"

// ═══════════════════════════════════════════════════════════════════
// UDP 命令分发器 —— 解析、校验、转换为 ControlCommand 并提交给统一协调层
//
// ★ Phase 5 改造（依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 5）：
//   - 不再持有 AbsMovePolicy/RelMovePolicy、不再直接写 PLC、不再阻塞式等待运动完成；
//   - 改为「解析 JSON → 构建 ControlCommand → MotionControlService::submit() → 立即
//     回 Queued + operationId」，运动由唯一协调层异步仲裁/执行；
//   - 新增 queryOperation(id) 回包（OperationEntry → JSON，状态含 Queued/Accepted/
//     Rejected/Running/Succeeded/Failed/TimedOut/CommitUncertain/Cancelled）。
//
// 查询类命令（GET_REL_POSITION / GET_MOVE_SPEED）不产生租约，直接从统一快照读取。
// ═══════════════════════════════════════════════════════════════════

// ============================================================
// UdpCommandDispatcher
// ============================================================
class UdpCommandDispatcher {
public:
    explicit UdpCommandDispatcher(application_vnext::control::MotionControlService& service)
        : m_service(service) {}

    /// @brief 处理一条 UDP 命令并返回 JSON 回复（非阻塞：运动命令立即回 Queued+operationId）
    std::string dispatch(const std::string& rawJson) {
        // ── 步骤 1：JSON 解析 ──
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(rawJson), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            LOG_WARN(LogLayer::APP, "UdpDispatcher",
                     "JSON parse failed: " + parseError.errorString().toStdString());
            return UdpResponseBuilder::buildRawError(0, 0, "invalid JSON format");
        }
        const QJsonObject req = doc.object();

        // ── 步骤 2：必填字段检查 ──
        if (!req.contains(QString::fromUtf8(UdpField::CMD))) {
            return UdpResponseBuilder::buildError(req, "missing required field 'cmd'");
        }
        const int cmdInt = req[QString::fromUtf8(UdpField::CMD)].toInt();

        // cmd=6 QUERY_OPERATION：只要求 cmd + operationId，不要求 motor/group。
        // 真实 UDP 客户端凭 submit 回执的 operationId 查询最终状态。
        if (cmdInt == static_cast<int>(UdpCmd::QUERY_OPERATION)) {
            if (!req.contains(QString::fromUtf8(UdpField::OP_ID))) {
                return UdpResponseBuilder::buildError(req,
                    "missing required field 'operationId' for cmd=6");
            }
            const std::string opId =
                req[QString::fromUtf8(UdpField::OP_ID)].toString().toStdString();
            LOG_INFO(LogLayer::APP, "UdpDispatcher",
                     "QUERY_OPERATION: opId=" + opId);
            return queryOperation(opId);
        }

        if (!req.contains(QString::fromUtf8(UdpField::MOTOR))) {
            return UdpResponseBuilder::buildError(req, "missing required field 'motor'");
        }
        if (!req.contains(QString::fromUtf8(UdpField::GROUP))) {
            return UdpResponseBuilder::buildError(req, "missing required field 'group'");
        }

        const int motor = req[QString::fromUtf8(UdpField::MOTOR)].toInt();
        const std::string groupName =
            req[QString::fromUtf8(UdpField::GROUP)].toString().toStdString();

        // ── 步骤 3：motor → AxisFunction 映射 ──
        auto fn = motorToFunction(motor);
        if (!fn) {
            std::string errMsg = "motor " + std::to_string(motor)
                + " not supported: only 0..5 (Y/Z/R/X/X1/X2)";
            LOG_WARN(LogLayer::APP, "UdpDispatcher", errMsg);
            return UdpResponseBuilder::buildError(req, errMsg);
        }

        // ── 步骤 4：group 名称 → 组索引 ──
        auto gIdx = groupNameToIndex(groupName);
        if (!gIdx) {
            std::string errMsg = "group '" + groupName + "' not recognized "
                                 "(expected Machine_A / Machine_B)";
            LOG_WARN(LogLayer::APP, "UdpDispatcher", errMsg);
            return UdpResponseBuilder::buildError(req, errMsg);
        }

        const UdpCmd cmd = static_cast<UdpCmd>(cmdInt);
        LOG_INFO(LogLayer::APP, "UdpDispatcher",
                 "dispatch: cmd=" + std::to_string(cmdInt)
                 + " motor=" + std::to_string(motor)
                 + " group=" + groupName);

        // ── 步骤 5：按 cmd 路由（构建 ControlCommand 或从快照查询）──
        switch (cmd) {
            case UdpCmd::MOVE_TO_REL_TARGET:  // cmd=0 -> 相对位置目标，换算绝对后 StartAbsMove
                return submitMotion(req, *fn, *gIdx, /*relTarget=*/true);
            case UdpCmd::MOVE_OFFSET:         // cmd=1 -> 相对偏移，换算绝对后 StartAbsMove
                return submitMotion(req, *fn, *gIdx, /*relTarget=*/false);
            case UdpCmd::GET_REL_POSITION:    // cmd=2 -> 快照查询
                return queryAxisField(req, *fn, *gIdx,
                    [](const AxisUiState& a) {
                        return QJsonValue(static_cast<double>(a.relPosition));
                    });
            case UdpCmd::SET_MOVE_SPEED:      // cmd=3 -> SetPositioningSpeed
                return submitSetSpeed(req, *fn, *gIdx);
            case UdpCmd::GET_MOVE_SPEED:      // cmd=4 -> 快照查询
                return queryAxisField(req, *fn, *gIdx,
                    [](const AxisUiState& a) {
                        return QJsonValue(static_cast<double>(a.positioningSpeed));
                    });
            case UdpCmd::SET_REL_ZERO:        // cmd=5 -> SetRelZero
                return submitSetRelZero(req, *fn, *gIdx);
            default: {
                std::string errMsg = "unknown cmd: " + std::to_string(cmdInt);
                LOG_WARN(LogLayer::APP, "UdpDispatcher", errMsg);
                return UdpResponseBuilder::buildError(req, errMsg);
            }
        }
    }

    /// @brief 查询某个 operationId 的异步操作状态（OperationEntry → JSON 回包）
    std::string queryOperation(const std::string& operationId) const {
        auto op = m_service.queryOperation(operationId);
        if (!op) {
            return UdpResponseBuilder::buildRawError(0, 0,
                "operation not found: " + operationId);
        }
        QJsonObject reply;
        reply[QString::fromUtf8(UdpField::RESULT)] = 1;
        reply[QString::fromUtf8(UdpField::OP_ID)] = QString::fromStdString(op->operationId);
        reply[QString::fromUtf8(UdpField::SOURCE)] = QString::fromUtf8(
            application_vnext::control::controlSourceName(op->source));
        reply[QString::fromUtf8(UdpField::AXIS)] = QString::fromStdString(op->axis);
        reply[QString::fromUtf8(UdpField::OP_KIND)] = QString::fromUtf8(
            operationKindName(op->kind));
        reply[QString::fromUtf8(UdpField::STATE)] = QString::fromUtf8(
            operationStateName(op->state));
        reply[QString::fromUtf8(UdpField::MOTION)] = op->motionState;
        // ★ 返回相对位置（R 轴 UDP 语义）：从统一快照取该轴 relPosition（PLC 反馈），
        //   而非 OperationEntry.position（其为绝对位置）。
        float posRel = op->position;
        {
            const auto snap = m_service.store().snapshot();
            for (const auto& a : snap.axes) {
                if (!a.bound) continue;
                const std::string key =
                    (a.group.value() == 0 ? "A." : "B.") + std::string(
                        domain_vnext::model::axisFunctionName(a.role));
                if (key == op->axis) { posRel = a.relPosition; break; }
            }
        }
        reply[QString::fromUtf8(UdpField::POS)] = static_cast<double>(posRel);
        reply[QString::fromUtf8(UdpField::DIAG)] = QString::fromStdString(op->diag);
        QJsonDocument doc(reply);
        return doc.toJson(QJsonDocument::Compact).toStdString();
    }

private:
    using AxisUiState = application_vnext::control::AxisUiState;
    using ControlSnapshot = application_vnext::control::ControlStateSnapshot;

    application_vnext::control::MotionControlService& m_service;

    /// 在统一快照中按「组索引 + 功能角色」查找轴 UI 状态。
    static const AxisUiState* findAxis(const ControlSnapshot& snap, int gIdx,
                                       domain_vnext::model::AxisFunction fn) {
        for (const auto& a : snap.axes) {
            if (a.bound && a.group.value() == gIdx && a.role == fn) return &a;
        }
        return nullptr;
    }

    /// 运动启动命令（cmd=0 / cmd=1）：统一换算为「绝对位置目标」后走 StartAbsMove，
    /// 立即回 Queued + operationId。
    /// ★ R 轴 UDP 语义（servoV6 定位按绝对位置执行）：
    ///   - cmd=0 MOVE_TO_REL_TARGET：`target` 是「相对位置目标」
    ///        → 绝对目标 = 相对原点(relZeroRecord) + target
    ///   - cmd=1 MOVE_OFFSET：`offset` 是「相对当前偏移」
    ///        → 绝对目标 = 当前绝对位置(absPosition) + offset
    /// 定位速度必填（Phase 4 P0：协调层权威校验 speed>0）：优先取请求中的 `speed`，
    /// 否则回退到快照中该轴当前 positioningSpeed；仍 <=0 则拒绝提交（绝不写 0 速度）。
    std::string submitMotion(const QJsonObject& req, domain_vnext::model::AxisFunction fn,
                             int gIdx, bool isRelTarget) {
        const char* field = isRelTarget ? UdpField::TARGET : UdpField::OFFSET;
        const char* cmdName = isRelTarget ? "cmd=0 (MOVE_TO_REL_TARGET)" : "cmd=1 (MOVE_OFFSET)";
        if (!req.contains(QString::fromUtf8(field))) {
            return UdpResponseBuilder::buildError(req,
                std::string("missing required field '") + field + "' for " + cmdName);
        }
        const float input = static_cast<float>(
            req[QString::fromUtf8(field)].toDouble());

        // 快照：既用于定位速度回退，也用于相对→绝对换算基准。
        const auto snap = m_service.store().snapshot();
        const AxisUiState* a = findAxis(snap, gIdx, fn);
        if (!a) {
            return UdpResponseBuilder::buildError(req,
                "axis not found/bound in unified snapshot");
        }

        // 定位速度：请求可带 `speed`，否则回退到快照中的定位速度。
        float speed = 0.0f;
        if (req.contains(QString::fromUtf8(UdpField::SPEED))) {
            speed = static_cast<float>(req[QString::fromUtf8(UdpField::SPEED)].toDouble());
        } else {
            speed = a->positioningSpeed;
        }
        if (speed <= 0.0f) {
            return UdpResponseBuilder::buildError(req,
                std::string("positioning speed must be positive for ") + cmdName
                + " (provide 'speed' or preset via SET_MOVE_SPEED)");
        }

        // ★ 相对 → 绝对换算（R 轴 UDP 语义）。
        float absTarget;
        if (isRelTarget) {
            absTarget = a->relZeroRecord + input;  // 相对位置目标 → 绝对目标
        } else {
            absTarget = a->absPosition + input;    // 相对当前偏移 → 绝对目标
        }

        application_vnext::control::ControlCommand c;
        c.source = application_vnext::control::ControlSource::Udp;
        c.target.group = plc_vnext::contracts::PlcGroupIndex(gIdx);
        c.target.function = fn;
        c.action = application_vnext::control::ControlAction::StartAbsMove;  // 统一绝对移动
        c.motion = application_vnext::control::MotionRequest{absTarget, speed};

        const std::string opId = m_service.submit(std::move(c));
        LOG_INFO(LogLayer::APP, "UdpDispatcher",
                 std::string("[") + cmdName + "] input=" + std::to_string(input)
                 + " -> absTarget=" + std::to_string(absTarget)
                 + " (relZero=" + std::to_string(a->relZeroRecord)
                 + ", abs=" + std::to_string(a->absPosition) + ") submitted, opId=" + opId);
        return queuedReply(req, opId);
    }

    /// SET_MOVE_SPEED（cmd=3）：SetPositioningSpeed 一次性写入。
    std::string submitSetSpeed(const QJsonObject& req,
                               domain_vnext::model::AxisFunction fn, int gIdx) {
        if (!req.contains(QString::fromUtf8(UdpField::SPEED))) {
            return UdpResponseBuilder::buildError(req,
                "missing required field 'speed' for cmd=3");
        }
        const float speed = static_cast<float>(
            req[QString::fromUtf8(UdpField::SPEED)].toDouble());
        // 与运动命令一致：定位速度必须为正，绝不写 0/负速度覆盖 PLC。
        if (speed <= 0.0f) {
            return UdpResponseBuilder::buildError(req,
                "positioning speed must be positive for cmd=3 (SET_MOVE_SPEED)");
        }

        application_vnext::control::ControlCommand c;
        c.source = application_vnext::control::ControlSource::Udp;
        c.target.group = plc_vnext::contracts::PlcGroupIndex(gIdx);
        c.target.function = fn;
        c.action = application_vnext::control::ControlAction::SetPositioningSpeed;
        c.value = speed;

        const std::string opId = m_service.submit(std::move(c));
        return queuedReply(req, opId);
    }

    /// SET_REL_ZERO（cmd=5）：SetRelZero 一次性写入。
    std::string submitSetRelZero(const QJsonObject& req,
                                 domain_vnext::model::AxisFunction fn, int gIdx) {
        application_vnext::control::ControlCommand c;
        c.source = application_vnext::control::ControlSource::Udp;
        c.target.group = plc_vnext::contracts::PlcGroupIndex(gIdx);
        c.target.function = fn;
        c.action = application_vnext::control::ControlAction::SetRelZero;

        const std::string opId = m_service.submit(std::move(c));
        return queuedReply(req, opId);
    }

    /// 查询类命令（cmd=2 / cmd=4）：从统一快照读取目标轴字段，不产生租约。
    template <typename FieldFn>
    std::string queryAxisField(const QJsonObject& req,
                               domain_vnext::model::AxisFunction fn, int gIdx,
                               FieldFn&& fieldFn) const {
        const auto snap = m_service.store().snapshot();
        const AxisUiState* a = findAxis(snap, gIdx, fn);
        if (!a) {
            return UdpResponseBuilder::buildError(req,
                "axis not found/bound in unified snapshot");
        }
        const std::string key =
            (gIdx == 0 ? "A." : "B.") + std::string(
                domain_vnext::model::axisFunctionName(fn));
        return UdpResponseBuilder::buildSuccess(req, {
            {std::string(UdpField::AXIS), QJsonValue(QString::fromStdString(key))},
            {std::string(UdpField::CURR), fieldFn(*a)},
        });
    }

    /// 统一构造「已提交」回包：回显请求 + result=1 + operationId + state=Queued。
    static std::string queuedReply(const QJsonObject& req, const std::string& opId) {
        return UdpResponseBuilder::buildSuccess(req, {
            {std::string(UdpField::OP_ID), QJsonValue(QString::fromStdString(opId))},
            {std::string(UdpField::STATE),
                QJsonValue(QString::fromUtf8(operationStateName(
                    application_vnext::control::OperationState::Queued)))},
        });
    }
};


