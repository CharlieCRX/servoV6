#include "presentation/viewmodel/GantryViewModelCore.h"
#include "domain/value/CouplingCondition.h"
#include "domain/value/GantryMode.h"
#include <cmath>

GantryViewModelCore::GantryViewModelCore(GantrySystem& gantry)
    : m_gantry(gantry)
    , m_prevMode(gantry.mode())
    , m_prevAggState(gantry.aggregatedState())
    , m_prevPos(gantry.position())
    , m_prevX1Enabled(gantry.x1Enabled())
    , m_prevX2Enabled(gantry.x2Enabled())
    , m_prevAnyAlarm(gantry.isAnyAlarm())
    , m_prevAnyLimit(gantry.isAnyLimit())
{
    LOG_INFO(LogLayer::UI, "GantryVM",
        "GantryViewModelCore created. Mode=" + std::string(::isCoupled(m_gantry.mode()) ? "Coupled" : "Decoupled") +
        " AggState=" + std::to_string(static_cast<int>(m_gantry.aggregatedState())) +
        " Pos=" + std::to_string(m_gantry.position()));
}

// ═══════════════════════════════════
// 1. 状态投影
// ═══════════════════════════════════

bool GantryViewModelCore::isCoupled() const {
    return m_gantry.isCoupled();
}

AxisState GantryViewModelCore::aggregatedState() const {
    return m_gantry.aggregatedState();
}

double GantryViewModelCore::position() const {
    return m_gantry.position();
}

double GantryViewModelCore::x1Position() const {
    return m_gantry.x1Position();
}

double GantryViewModelCore::x2Position() const {
    return m_gantry.x2Position();
}

bool GantryViewModelCore::x1Enabled() const {
    return m_gantry.x1Enabled();
}

bool GantryViewModelCore::x2Enabled() const {
    return m_gantry.x2Enabled();
}

bool GantryViewModelCore::isAnyAlarm() const {
    return m_gantry.isAnyAlarm();
}

bool GantryViewModelCore::isAnyLimit() const {
    return m_gantry.isAnyLimit();
}

bool GantryViewModelCore::x1PosLimit() const {
    return m_gantry.x1().isPosLimitActive();
}

bool GantryViewModelCore::x1NegLimit() const {
    return m_gantry.x1().isNegLimitActive();
}

bool GantryViewModelCore::x2PosLimit() const {
    return m_gantry.x2().isPosLimitActive();
}

bool GantryViewModelCore::x2NegLimit() const {
    return m_gantry.x2().isNegLimitActive();
}

bool GantryViewModelCore::canAcceptCommand() const {
    return m_gantry.canAcceptCommand();
}

std::string GantryViewModelCore::stateDescription() const {
    return m_gantry.stateDescription();
}

bool GantryViewModelCore::canCouple() const {
    auto result = CouplingCondition::checkAll(
        m_gantry.x1().isEnabled(),
        m_gantry.x2().isEnabled(),
        m_gantry.isAnyAlarm(),
        m_gantry.isAnyLimit(),
        m_gantry.x1Position(),
        m_gantry.x2Position()
    );
    return result.allowed;
}

// ═══════════════════════════════════
// 2. 控制指令
// ═══════════════════════════════════

CouplingCondition::Result GantryViewModelCore::requestCoupling() {
    auto result = m_gantry.requestCoupling();
    if (result.allowed) {
        LOG_INFO(LogLayer::UI, "GantryVM", "requestCoupling → ACCEPTED");
        m_lastCommandResult = "Coupling: Accepted";
    } else {
        LOG_WARN(LogLayer::UI, "GantryVM", "requestCoupling → REJECTED: " + result.failReason);
        m_lastCommandResult = "Coupling: Rejected — " + result.failReason;
    }
    return result;
}

void GantryViewModelCore::requestDecoupling(const std::string& reason) {
    std::string fullReason = reason.empty() ? "UI manual decouple" : reason;
    m_gantry.requestDecoupling(fullReason);
    LOG_INFO(LogLayer::UI, "GantryVM",
        "requestDecoupling: " + fullReason +
        " Mode=" + std::string(::isCoupled(m_gantry.mode()) ? "Coupled" : "Decoupled"));
}

void GantryViewModelCore::jogX1Forward() {
    auto result = m_gantry.jog(AxisId::X1, MotionDirection::Forward);
    logCommandResult("jogX1Forward", result);
}

void GantryViewModelCore::jogX1Reverse() {
    auto result = m_gantry.jog(AxisId::X1, MotionDirection::Backward);
    logCommandResult("jogX1Reverse", result);
}

void GantryViewModelCore::jogX2Forward() {
    auto result = m_gantry.jog(AxisId::X2, MotionDirection::Forward);
    logCommandResult("jogX2Forward", result);
}

void GantryViewModelCore::jogX2Reverse() {
    auto result = m_gantry.jog(AxisId::X2, MotionDirection::Backward);
    logCommandResult("jogX2Reverse", result);
}

void GantryViewModelCore::jogCoupledForward() {
    auto result = m_gantry.jog(AxisId::X, MotionDirection::Forward);
    logCommandResult("jogCoupledForward", result);
}

void GantryViewModelCore::jogCoupledReverse() {
    auto result = m_gantry.jog(AxisId::X, MotionDirection::Backward);
    logCommandResult("jogCoupledReverse", result);
}

void GantryViewModelCore::jogCoupledRelease() {
    auto result = m_gantry.stop(AxisId::X);
    LOG_INFO(LogLayer::UI, "GantryVM", "jogCoupledRelease — stop sent to logical axis X");
}

void GantryViewModelCore::moveAbsoluteX1(double targetPos) {
    auto result = m_gantry.moveAbsolute(AxisId::X1, targetPos);
    logCommandResult("moveAbsoluteX1(" + std::to_string(targetPos) + ")", result);
}

void GantryViewModelCore::moveAbsoluteX2(double targetPos) {
    auto result = m_gantry.moveAbsolute(AxisId::X2, targetPos);
    logCommandResult("moveAbsoluteX2(" + std::to_string(targetPos) + ")", result);
}

void GantryViewModelCore::moveAbsoluteCoupled(double targetPos) {
    auto result = m_gantry.moveAbsolute(AxisId::X, targetPos);
    logCommandResult("moveAbsoluteCoupled(" + std::to_string(targetPos) + ")", result);
}

void GantryViewModelCore::moveRelativeX1(double delta) {
    auto result = m_gantry.moveRelative(AxisId::X1, delta);
    logCommandResult("moveRelativeX1(" + std::to_string(delta) + ")", result);
}

void GantryViewModelCore::moveRelativeX2(double delta) {
    auto result = m_gantry.moveRelative(AxisId::X2, delta);
    logCommandResult("moveRelativeX2(" + std::to_string(delta) + ")", result);
}

void GantryViewModelCore::moveRelativeCoupled(double delta) {
    auto result = m_gantry.moveRelative(AxisId::X, delta);
    logCommandResult("moveRelativeCoupled(" + std::to_string(delta) + ")", result);
}

void GantryViewModelCore::stop() {
    if (::isCoupled(m_gantry.mode())) {
        m_gantry.stop(AxisId::X);
        LOG_INFO(LogLayer::UI, "GantryVM", "stop — logical axis X (Coupled mode)");
    } else {
        m_gantry.stop(AxisId::X1);
        m_gantry.stop(AxisId::X2);
        LOG_INFO(LogLayer::UI, "GantryVM", "stop — physical axes X1+X2 (Decoupled mode)");
    }
}

// ═══════════════════════════════════
// 3. 事件管理与日志
// ═══════════════════════════════════

std::vector<GantryEvents::Event> GantryViewModelCore::drainEvents() {
    return m_gantry.drainEvents();
}

const std::vector<GantryEvents::Event>& GantryViewModelCore::events() const {
    return m_gantry.events();
}

std::string GantryViewModelCore::lastCommandResult() const {
    return m_lastCommandResult;
}

std::string GantryViewModelCore::getCommandLog() const {
    std::ostringstream oss;
    for (const auto& line : m_commandLog) {
        oss << line << "\n";
    }
    return oss.str();
}

// ═══════════════════════════════════
// 4. Tick 驱动
// ═══════════════════════════════════

void GantryViewModelCore::tick() {
    // 1. 执行状态聚合
    m_gantry.aggregateState();

    // 2. 检测状态变更并写日志
    detectAndLogStateChanges();

    // 3. 收集领域事件并写入日志
    auto drainedEvents = m_gantry.drainEvents();
    for (const auto& evt : drainedEvents) {
        if (!evt.isNone()) {
            LogLevel level = LogLevel::INFO;
            if (evt.type == GantryEvents::Type::DeviationFault ||
                evt.type == GantryEvents::Type::LimitTriggered ||
                evt.type == GantryEvents::Type::AlarmRaised ||
                evt.type == GantryEvents::Type::CommandRejected) {
                level = LogLevel::ERROR;
            }
            Logger::log(level, LogLayer::DOM, "GantryEvent",
                "Event: " + evt.description);
        }
    }
}

// ═══════════════════════════════════
// Private helpers
// ═══════════════════════════════════

void GantryViewModelCore::logCommandResult(const std::string& cmdName, const CommandResult& result) {
    if (result.accepted) {
        LOG_INFO(LogLayer::UI, "GantryVM", cmdName + " → ACCEPTED");
        m_lastCommandResult = cmdName + ": Accepted";
        m_commandLog.push_back("[OK] " + cmdName);
    } else {
        std::string reason = result.rejectReason.empty() ? "Unknown" : result.rejectReason;
        LOG_WARN(LogLayer::UI, "GantryVM", cmdName + " → REJECTED: " + reason);
        m_lastCommandResult = cmdName + ": Rejected — " + reason;
        m_commandLog.push_back("[REJ] " + cmdName + " (" + reason + ")");
    }
    // 环形缓冲：超过最大值时丢弃最旧的条目
    while (m_commandLog.size() > kCommandLogMaxEntries) {
        m_commandLog.pop_front();
    }
}

void GantryViewModelCore::detectAndLogStateChanges() {
    // 模式变更
    auto currentMode = m_gantry.mode();
    if (currentMode != m_prevMode) {
        LOG_SUMMARY(LogLayer::DOM, "GantryVM",
            "Mode changed: " + std::string(m_prevMode == GantryMode::Coupled ? "Coupled" : "Decoupled") +
            " → " + std::string(currentMode == GantryMode::Coupled ? "Coupled" : "Decoupled"));
        m_prevMode = currentMode;
    }

    // 聚合状态变更
    auto currentAggState = m_gantry.aggregatedState();
    if (currentAggState != m_prevAggState) {
        LOG_SUMMARY(LogLayer::DOM, "GantryVM",
            "AggState changed: " + std::to_string(static_cast<int>(m_prevAggState)) +
            " → " + std::to_string(static_cast<int>(currentAggState)) +
            " Desc=" + m_gantry.stateDescription());
        m_prevAggState = currentAggState;
    }

    // 位置显著变化 (>0.1mm)
    auto currentPos = m_gantry.position();
    if (std::abs(currentPos - m_prevPos) > 0.1) {
        LOG_TRACE(LogLayer::DOM, "GantryVM",
            "Position: " + std::to_string(currentPos) +
            " (X1=" + std::to_string(m_gantry.x1Position()) +
            " X2=" + std::to_string(m_gantry.x2Position()) + ")");
        m_prevPos = currentPos;
    }

    // 使能状态变更
    bool currentX1En = m_gantry.x1Enabled();
    if (currentX1En != m_prevX1Enabled) {
        LOG_INFO(LogLayer::DOM, "GantryVM",
            "X1 Enabled: " + std::string(currentX1En ? "true" : "false"));
        m_prevX1Enabled = currentX1En;
    }
    bool currentX2En = m_gantry.x2Enabled();
    if (currentX2En != m_prevX2Enabled) {
        LOG_INFO(LogLayer::DOM, "GantryVM",
            "X2 Enabled: " + std::string(currentX2En ? "true" : "false"));
        m_prevX2Enabled = currentX2En;
    }

    // 报警状态变更
    bool currentAlarm = m_gantry.isAnyAlarm();
    if (currentAlarm != m_prevAnyAlarm) {
        if (currentAlarm) {
            LOG_ERROR(LogLayer::DOM, "GantryVM", "ALARM triggered!");
        } else {
            LOG_INFO(LogLayer::DOM, "GantryVM", "Alarm cleared");
        }
        m_prevAnyAlarm = currentAlarm;
    }

    // 限位状态变更
    bool currentLimit = m_gantry.isAnyLimit();
    if (currentLimit != m_prevAnyLimit) {
        if (currentLimit) {
            LOG_ERROR(LogLayer::DOM, "GantryVM", "LIMIT triggered!");
        } else {
            LOG_INFO(LogLayer::DOM, "GantryVM", "Limit cleared");
        }
        m_prevAnyLimit = currentLimit;
    }
}
