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

#include <variant>
#include <utility>

// =============================================================================
// 内部辅助：通过 SystemManager 定位目标轴
// =============================================================================

namespace {

/**
 * @brief 通过 SystemManager → SystemContext → Axis 定位目标轴
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

}  // anonymous namespace

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
}

AxisViewModelCore::~AxisViewModelCore() = default;

// =============================================================================
// 1. 状态投影
// =============================================================================

AxisState AxisViewModelCore::state() const
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return AxisState::Unknown;
    return axis->state();
}

double AxisViewModelCore::absPos() const
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return 0.0;
    return axis->currentAbsolutePosition();
}

double AxisViewModelCore::relPos() const
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return 0.0;
    return axis->currentRelativePosition();
}

bool AxisViewModelCore::isEnabled() const
{
    return state() != AxisState::Disabled;
}

double AxisViewModelCore::jogVelocity() const
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return 0.0;
    return axis->getjogVelocity();
}

double AxisViewModelCore::moveVelocity() const
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return 0.0;
    return axis->getMoveVelocity();
}

double AxisViewModelCore::posLimit() const
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return 0.0;
    return axis->positiveSoftLimit();
}

double AxisViewModelCore::negLimit() const
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return 0.0;
    return axis->negativeSoftLimit();
}

// =============================================================================
// 2. 错误接口
// =============================================================================

bool AxisViewModelCore::hasError() const
{
    return m_hasError;
}

ViewModelError AxisViewModelCore::lastError() const
{
    return m_lastError;
}

void AxisViewModelCore::clearError()
{
    m_hasError = false;
    m_lastError = {};
}

// =============================================================================
// 内部辅助：执行 UseCase 并翻译错误
// =============================================================================

namespace {

/**
 * @brief 执行一个返回 UseCaseError 的操作，并将错误翻译存储到 ViewModel
 * @return true = 成功（monostate），false = 有错误（已存入 vmError）
 */
bool executeAndTranslate(UseCaseError result,
                         ViewModelError& vmError,
                         bool& hasError)
{
    if (std::holds_alternative<std::monostate>(result)) {
        return true;  // 成功
    }

    vmError = translate(result);
    hasError = true;
    return false;
}

}  // anonymous namespace

// =============================================================================
// 3. 控制指令
// =============================================================================

void AxisViewModelCore::enable(bool active)
{
    auto result = m_enableUc->execute(m_manager, m_groupName, m_axisId, active);
    executeAndTranslate(result, m_lastError, m_hasError);
}

void AxisViewModelCore::disable()
{
    enable(false);
}

void AxisViewModelCore::jog(Direction dir)
{
    m_jogOrch->startJog(m_axisId, dir);
}

void AxisViewModelCore::jogStop(Direction dir)
{
    m_jogOrch->stopJog(m_axisId, dir);
}

void AxisViewModelCore::moveAbsolute(double targetPos)
{
    m_absOrch->startAbs(m_axisId, targetPos);
}

void AxisViewModelCore::moveRelative(double distance)
{
    m_relOrch->startRel(m_axisId, distance);
}

void AxisViewModelCore::stop()
{
    // 1. 下发停止命令到硬件（通过 UseCase）
    auto result = m_stopUc->execute(m_manager, m_groupName, m_axisId);
    executeAndTranslate(result, m_lastError, m_hasError);

    // 2. 中断正在进行的点动编排器
    //    注：Abs/Rel 编排器无显式 stop() 入口，
    //    它们会在 tick() 中因 Axis 回到 Idle 而自行收敛
    if (m_jogOrch->currentStep() != JogOrchestrator::Step::Done &&
        m_jogOrch->currentStep() != JogOrchestrator::Step::Error &&
        m_jogOrch->currentStep() != JogOrchestrator::Step::Idle) {
        // 通过 stopJog 触发编排器内部 IssuingStop → WaitingForIdle → EnsuringDisabled 流程
        // 任一方向均可触发停止（编排器会校验 Direction 匹配）
        m_jogOrch->stopJog(m_axisId, Direction::Forward);
    }
}

void AxisViewModelCore::setJogVelocity(double v)
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (axis) {
        axis->setJogVelocity(v);
    }
}

void AxisViewModelCore::setMoveVelocity(double v)
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (axis) {
        axis->setMoveVelocity(v);
    }
}

// =============================================================================
// 4. 零位操作
// =============================================================================

void AxisViewModelCore::zeroAbsolutePosition()
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (axis) {
        axis->zeroAbsolutePosition();
    }
}

void AxisViewModelCore::setRelativeZero()
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (axis) {
        axis->setRelativeZero();
    }
}

void AxisViewModelCore::clearRelativeZero()
{
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (axis) {
        axis->clearRelativeZero();
    }
}

// =============================================================================
// 5. 帧驱动
// =============================================================================

void AxisViewModelCore::tick()
{
    // 驱动所有编排器状态机
    m_jogOrch->tick();
    m_absOrch->tick();
    m_relOrch->tick();

    // 收集并翻译各编排器的错误
    // 优先级：Jog > Abs > Rel（最后出错者覆盖）
    if (m_jogOrch->hasError()) {
        m_lastError = translate(m_jogOrch->lastError());
        m_hasError = true;
    }
    if (m_absOrch->hasError()) {
        m_lastError = translate(m_absOrch->lastError());
        m_hasError = true;
    }
    if (m_relOrch->hasError()) {
        m_lastError = translate(m_relOrch->lastError());
        m_hasError = true;
    }

    // 发送零位/速度类命令的 pending command（此类操作没有独立的 Orchestrator，
    // 命令暂存在 Axis::pending_intent 中，需在此处消费并发送到 driver。
    // 运动类命令（Jog/Move/Stop）由各 Orchestrator 负责发送，此处不再重复处理）
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (axis) {
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (m_manager.tryGetGroup(m_groupName, group, mgrReason) && group) {
            if (auto* drv = group->driver()) {
                const AxisCommand& cmd = axis->getPendingCommand();
                // 仅零位/速度类命令由此处发送
                bool isZeroOrVelocity =
                    std::holds_alternative<ZeroAbsoluteCommand>(cmd) ||
                    std::holds_alternative<SetRelativeZeroCommand>(cmd) ||
                    std::holds_alternative<ClearRelativeZeroCommand>(cmd) ||
                    std::holds_alternative<SetJogVelocityCommand>(cmd) ||
                    std::holds_alternative<SetMoveVelocityCommand>(cmd);
                if (isZeroOrVelocity) {
                    auto commResult = drv->send(AxisCommandWithId{m_axisId, cmd});
                    if (!commResult.ok()) {
                        m_lastError = ViewModelError{"ZERO_CMD_FAILED",
                                                     "Zero/Velocity command delivery failed",
                                                     "Zero/Velocity command delivery failed",
                                                     ErrorCategory::Modal};
                        m_hasError = true;
                    }
                }
            }
        }
    }
}
