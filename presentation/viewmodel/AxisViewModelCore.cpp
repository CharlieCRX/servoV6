#include "AxisViewModelCore.h"

#include "application/SystemManager.h"
#include "application/axis/EnableUseCase.h"
#include "application/axis/JogAxisUseCase.h"
#include "application/axis/MoveAbsoluteUseCase.h"
#include "application/axis/MoveRelativeUseCase.h"
#include "application/axis/StopAxisUseCase.h"
#include "application/policy/AutoAbsMoveOrchestrator.h"
#include "application/policy/AutoRelMoveOrchestrator.h"
#include "application/policy/JogOrchestrator.h"
#include "domain/entity/Axis.h"
#include "domain/entity/SystemContext.h"
#include "ErrorTranslator.h"
#include "ViewModelError.h"

#include "infrastructure/logger/Logger.h"
#include "infrastructure/logger/TraceScope.h"
#include "infrastructure/utils/CommandFormatter.h"

#include <variant>
#include <utility>
#include <algorithm>

// =============================================================================
// 内部辅助：通过 SystemManager 定位目标轴
// =============================================================================

namespace {

/**
 * @brief 通过 SystemManager -> SystemContext -> Axis 定位目标轴（控制操作入口）
 *        受 Layer 0 安全锁定拦截。
 * @return Axis* 若成功，nullptr 若查找失败
 */
Axis* tryGetAxis(SystemManager& manager,
                 const std::string& groupName,
                 AxisId axisId)
{
    SystemContext* group = nullptr;
    ContextRejection mgrReason = ContextRejection::None;
    if (!manager.tryGetGroup(groupName, group, mgrReason)) {
        return nullptr;
    }

    Axis* axis = nullptr;
    ContextRejection ctxReason = ContextRejection::None;
    if (!group->tryGetAxis(axisId, axis, ctxReason)) {
        return nullptr;
    }

    return axis;
}

/**
 * @brief 通过 SystemManager -> SystemContext -> Axis 定位目标轴（遥测读取入口）
 *        绕过 Layer 0 安全锁定拦截，急停期间仍可读取位置与状态。
 * @return Axis* 若成功，nullptr 若龙门语义/容器查找失败
 */
Axis* tryReadAxis(SystemManager& manager,
                  const std::string& groupName,
                  AxisId axisId)
{
    SystemContext* group = nullptr;
    ContextRejection mgrReason = ContextRejection::None;
    if (!manager.tryGetGroup(groupName, group, mgrReason)) {
        return nullptr;
    }

    Axis* axis = nullptr;
    ContextRejection ctxReason = ContextRejection::None;
    if (!group->tryReadAxis(axisId, axis, ctxReason)) {
        return nullptr;
    }

    return axis;
}

}  // anonymous namespace

// =============================================================================
// TraceScope / 日志辅助方法
// =============================================================================

std::string AxisViewModelCore::generateTraceId()
{
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::steady_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    return std::to_string(ns) + "_" + std::to_string(++counter);
}

std::string AxisViewModelCore::axisIdToString(AxisId id)
{
    switch (id) {
    case AxisId::X1: return "X1";
    case AxisId::X2: return "X2";
    case AxisId::Y:  return "Y";
    case AxisId::Z:  return "Z";
    case AxisId::R:  return "R";
    case AxisId::X:  return "X";
    default:         return "Unknown";
    }
}

std::string AxisViewModelCore::logPrefix() const
{
    return m_groupName + "/" + axisIdToString(m_axisId);
}

// =============================================================================
// 构造 / 析构
// =============================================================================

AxisViewModelCore::AxisViewModelCore(SystemManager& manager,
                                     const std::string& groupName,
                                     AxisId axisId)
    : m_manager(manager)
    , m_groupName(groupName)
    , m_axisId(axisId)
    , m_enableUc(std::make_unique<EnableUseCase>())
    , m_jogUc(std::make_unique<JogAxisUseCase>())
    , m_moveAbsUc(std::make_unique<MoveAbsoluteUseCase>())
    , m_moveRelUc(std::make_unique<MoveRelativeUseCase>())
    , m_stopUc(std::make_unique<StopAxisUseCase>())
    , m_jogOrch(std::make_unique<JogOrchestrator>(manager, groupName))
    , m_absOrch(std::make_unique<AutoAbsMoveOrchestrator>(manager, groupName))
    , m_relOrch(std::make_unique<AutoRelMoveOrchestrator>(manager, groupName))
{
    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " ViewModel created");
}

AxisViewModelCore::~AxisViewModelCore() = default;

// =============================================================================
// 1. 状态投影
// =============================================================================

AxisState AxisViewModelCore::state() const
{
    auto* axis = tryReadAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return AxisState::Unknown;
    return axis->state();
}

double AxisViewModelCore::absPos() const
{
    auto* axis = tryReadAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return 0.0;
    return axis->currentAbsolutePosition();
}

double AxisViewModelCore::relPos() const
{
    auto* axis = tryReadAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return 0.0;
    return axis->currentRelativePosition();
}

bool AxisViewModelCore::isEnabled() const
{
    return state() != AxisState::Disabled;
}

double AxisViewModelCore::jogVelocity() const
{
    auto* axis = tryReadAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return 0.0;
    return axis->getjogVelocity();
}

double AxisViewModelCore::moveVelocity() const
{
    auto* axis = tryReadAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return 0.0;
    return axis->getMoveVelocity();
}

double AxisViewModelCore::posLimit() const
{
    auto* axis = tryReadAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return 0.0;
    return axis->positiveSoftLimit();
}

double AxisViewModelCore::negLimit() const
{
    auto* axis = tryReadAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return 0.0;
    return axis->negativeSoftLimit();
}

// =============================================================================
// 2. 错误接口（列表收集模式）
// =============================================================================

bool AxisViewModelCore::hasError() const
{
    return !m_errorHistory.empty();
}

ViewModelError AxisViewModelCore::lastError() const
{
    if (m_errorHistory.empty()) {
        return {};
    }
    return m_errorHistory.back().error;
}

size_t AxisViewModelCore::errorCount() const
{
    return m_errorHistory.size();
}

std::vector<ViewModelError> AxisViewModelCore::allErrors() const
{
    std::vector<ViewModelError> result;
    result.reserve(m_errorHistory.size());
    for (const auto& entry : m_errorHistory) {
        result.push_back(entry.error);
    }
    return result;
}

void AxisViewModelCore::acknowledgeError(size_t index)
{
    if (index < m_errorHistory.size()) {
        LOG_DEBUG(LogLayer::UI, "AxisVM",
            logPrefix() + " acknowledgeError index=" + std::to_string(index)
            + " code=" + m_errorHistory[index].error.code);
        m_errorHistory.erase(m_errorHistory.begin() + static_cast<ptrdiff_t>(index));
    }
}

void AxisViewModelCore::clearAllErrors()
{
    if (!m_errorHistory.empty()) {
        LOG_DEBUG(LogLayer::UI, "AxisVM",
            logPrefix() + " clearAllErrors count=" + std::to_string(m_errorHistory.size()));
        m_errorHistory.clear();
    }
}

void AxisViewModelCore::clearError()
{
    clearAllErrors();
}

void AxisViewModelCore::pushError(const ViewModelError& error, const std::string& source)
{
    if (!error.isValid()) return;
    m_errorHistory.push_back({
        .error = error,
        .timestamp = std::chrono::steady_clock::now(),
        .source = source
    });
    LOG_WARN(LogLayer::UI, "AxisVM",
        logPrefix() + " error from " + source + ": " + error.code
        + " - " + error.debugMessage);
}

// =============================================================================
// 3. 控制指令（带 TraceScope + 日志 + 错误保护）
// =============================================================================

void AxisViewModelCore::enable(bool active)
{
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());

    if (!active) {
        // disable: 直接执行，不检查/覆盖错误
        m_enableUc->execute(m_manager, m_groupName, m_axisId, false);
        LOG_INFO(LogLayer::UI, "AxisVM",
            logPrefix() + " disable requested");
        return;
    }

    // enable: 正常收集错误
    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " enable requested");

    auto result = m_enableUc->execute(m_manager, m_groupName, m_axisId, true);
    if (!std::holds_alternative<std::monostate>(result)) {
        auto vmError = translate(result);
        if (vmError.isValid()) {
            pushError(vmError, "EnableUC");
            LOG_ERROR(LogLayer::UI, "AxisVM",
                logPrefix() + " enable failed: " + vmError.code);
        }
    } else {
        LOG_DEBUG(LogLayer::UI, "AxisVM",
            logPrefix() + " enable accepted");
    }
}

void AxisViewModelCore::disable()
{
    enable(false);
}

void AxisViewModelCore::jog(Direction dir)
{
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());

    const char* dirStr = (dir == Direction::Forward) ? "Forward" : "Backward";
    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " jog " + dirStr + " pressed");

    m_jogOrch->startJog(m_axisId, dir);

    if (m_jogOrch->hasError()) {
        auto vmError = translate(m_jogOrch->lastError());
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " jog " + dirStr + " rejected at start: " + vmError.code);
    }
}

void AxisViewModelCore::jogStop(Direction dir)
{
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());

    const char* dirStr = (dir == Direction::Forward) ? "Forward" : "Backward";
    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " jog " + dirStr + " released");

    m_jogOrch->stopJog(m_axisId, dir);
}

void AxisViewModelCore::moveAbsolute(double targetPos)
{
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());

    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " moveAbsolute target=" + std::to_string(targetPos));

    m_absOrch->startAbs(m_axisId, targetPos);

    if (m_absOrch->hasError()) {
        auto vmError = translate(m_absOrch->lastError());
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " moveAbsolute rejected at start: " + vmError.code);
    }
}

void AxisViewModelCore::moveRelative(double distance)
{
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());

    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " moveRelative distance=" + std::to_string(distance));

    m_relOrch->startRel(m_axisId, distance);

    if (m_relOrch->hasError()) {
        auto vmError = translate(m_relOrch->lastError());
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " moveRelative rejected at start: " + vmError.code);
    }
}

void AxisViewModelCore::stop()
{
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());

    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " stop pressed (may interrupt active motion)");

    // 1. 下发停止命令到硬件（通过 UseCase）
    auto result = m_stopUc->execute(m_manager, m_groupName, m_axisId);
    if (!std::holds_alternative<std::monostate>(result)) {
        auto vmError = translate(result);
        pushError(vmError, "StopUC");
        LOG_ERROR(LogLayer::UI, "AxisVM",
            logPrefix() + " stop command failed: " + vmError.code);
    }

    // 2. 中断正在进行的点动编排器
    //    注：Abs/Rel 编排器无显式 stop() 入口，
    //    它们会在 tick() 中因 Axis 回到 Idle 而自行收敛
    if (m_jogOrch->currentStep() != JogOrchestrator::Step::Done &&
        m_jogOrch->currentStep() != JogOrchestrator::Step::Error &&
        m_jogOrch->currentStep() != JogOrchestrator::Step::Idle) {
        m_jogOrch->stopJog(m_axisId, Direction::Forward);
        LOG_DEBUG(LogLayer::UI, "AxisVM",
            logPrefix() + " jog orchestrator interrupted by stop");
    }
}

void AxisViewModelCore::setJogVelocity(double v)
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) {
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " setJogVelocity(" + std::to_string(v)
            + ") failed: axis not found");
        return;
    }

    if (!axis->setJogVelocity(v)) {
        auto vmError = translate(UseCaseError{axis->lastRejection()});
        pushError(vmError, "SetJogVel");
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " setJogVelocity(" + std::to_string(v)
            + ") rejected: " + vmError.code);
    } else {
        LOG_DEBUG(LogLayer::UI, "AxisVM",
            logPrefix() + " jogVelocity set to " + std::to_string(v));
    }
}

void AxisViewModelCore::setMoveVelocity(double v)
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) {
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " setMoveVelocity(" + std::to_string(v)
            + ") failed: axis not found");
        return;
    }

    if (!axis->setMoveVelocity(v)) {
        auto vmError = translate(UseCaseError{axis->lastRejection()});
        pushError(vmError, "SetMoveVel");
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " setMoveVelocity(" + std::to_string(v)
            + ") rejected: " + vmError.code);
    } else {
        LOG_DEBUG(LogLayer::UI, "AxisVM",
            logPrefix() + " moveVelocity set to " + std::to_string(v));
    }
}

// =============================================================================
// 4. 零位操作（带错误保护 + 日志）
// =============================================================================

void AxisViewModelCore::zeroAbsolutePosition()
{
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());
    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " zeroAbsolutePosition requested");

    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) {
        auto error = ViewModelError{
            "CTX_AXIS_NOT_REGISTERED",
            "轴未注册，无法执行零位操作",
            logPrefix() + " not found in system context",
            ErrorCategory::Modal
        };
        pushError(error, "ZeroAbsOp");
        return;
    }

    if (!axis->zeroAbsolutePosition()) {
        auto vmError = translate(UseCaseError{axis->lastRejection()});
        pushError(vmError, "ZeroAbsOp");
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " zeroAbsolutePosition rejected: " + vmError.code);
    } else {
        LOG_DEBUG(LogLayer::UI, "AxisVM",
            logPrefix() + " zeroAbsolutePosition accepted, pending command queued");
    }
}

void AxisViewModelCore::setRelativeZero()
{
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());
    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " setRelativeZero requested");

    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) {
        auto error = ViewModelError{
            "CTX_AXIS_NOT_REGISTERED",
            "轴未注册，无法设置相对零位",
            logPrefix() + " not found in system context",
            ErrorCategory::Modal
        };
        pushError(error, "SetRelZero");
        return;
    }

    if (!axis->setRelativeZero()) {
        auto vmError = translate(UseCaseError{axis->lastRejection()});
        pushError(vmError, "SetRelZero");
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " setRelativeZero rejected: " + vmError.code);
    } else {
        LOG_DEBUG(LogLayer::UI, "AxisVM",
            logPrefix() + " setRelativeZero accepted, pending command queued");
    }
}

void AxisViewModelCore::clearRelativeZero()
{
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());
    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " clearRelativeZero requested");

    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) {
        auto error = ViewModelError{
            "CTX_AXIS_NOT_REGISTERED",
            "轴未注册，无法清除相对零位",
            logPrefix() + " not found in system context",
            ErrorCategory::Modal
        };
        pushError(error, "ClearRelZero");
        return;
    }

    if (!axis->clearRelativeZero()) {
        auto vmError = translate(UseCaseError{axis->lastRejection()});
        pushError(vmError, "ClearRelZero");
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " clearRelativeZero rejected: " + vmError.code);
    } else {
        LOG_DEBUG(LogLayer::UI, "AxisVM",
            logPrefix() + " clearRelativeZero accepted, pending command queued");
    }
}

// =============================================================================
// 5. 帧驱动
// =============================================================================

void AxisViewModelCore::tick()
{
    // Step 1: 驱动所有编排器状态机
    m_jogOrch->tick();
    m_absOrch->tick();
    m_relOrch->tick();

    // Step 2: 收集错误（追加模式，不再覆盖）
    collectOrchError(*m_jogOrch, "JogOrch");
    collectOrchError(*m_absOrch, "AbsOrch");
    collectOrchError(*m_relOrch, "RelOrch");

    // Step 3: 消费零位/速度类 pending command
    consumePendingCommands();

    // Step 4: 日志摘要（每 100 帧输出一次）
    LOG_TRACE_EVERY_N(100, LogLayer::UI, "AxisVM",
        logPrefix()
        + " tick: state=" + std::to_string(static_cast<int>(state()))
        + " errors=" + std::to_string(m_errorHistory.size()));
}

void AxisViewModelCore::consumePendingCommands()
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) {
        return;
    }

    if (!axis->hasPendingCommand()) {
        return;
    }

    const AxisCommand& cmd = axis->getPendingCommand();
    bool isZeroOrVelocity =
        std::holds_alternative<ZeroAbsoluteCommand>(cmd) ||
        std::holds_alternative<SetRelativeZeroCommand>(cmd) ||
        std::holds_alternative<ClearRelativeZeroCommand>(cmd) ||
        std::holds_alternative<SetJogVelocityCommand>(cmd) ||
        std::holds_alternative<SetMoveVelocityCommand>(cmd);

    if (!isZeroOrVelocity) {
        return;  // 运动类命令由 Orchestrator 处理
    }

    // 获取 driver
    SystemContext* group = nullptr;
    ContextRejection mgrReason = ContextRejection::None;
    if (!m_manager.tryGetGroup(m_groupName, group, mgrReason) || !group) {
        LOG_ERROR(LogLayer::UI, "AxisVM",
            logPrefix() + " cannot send command: group not found");
        return;
    }

    auto* drv = group->driver();
    if (!drv) {
        LOG_ERROR(LogLayer::UI, "AxisVM",
            logPrefix() + " cannot send command: driver not ready");
        return;
    }

    // 使用 CommandFormatter 记录下发的命令详情
    LOG_DEBUG(LogLayer::UI, "AxisVM",
        logPrefix() + " sending: " + utils::format(cmd));

    auto commResult = drv->send(AxisCommandWithId{m_axisId, cmd});
    if (!commResult.ok()) {
        auto vmError = ViewModelError{
            "ZERO_CMD_FAILED",
            "零位/速度命令下发失败",
            commResult.diagnostic,
            ErrorCategory::Modal
        };
        pushError(vmError, "CmdDelivery");
        LOG_ERROR(LogLayer::UI, "AxisVM",
            logPrefix() + " command delivery failed: " + commResult.diagnostic);
    } else {
        LOG_TRACE(LogLayer::UI, "AxisVM",
            logPrefix() + " command delivered successfully");
    }
}
