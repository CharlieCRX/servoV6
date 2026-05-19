#include "presentation/viewmodel/GantryViewModel.h"
#include "application/SystemManager.h"
#include "application/policy/GantryOrchestrator.h"
#include "domain/entity/SystemContext.h"
#include "domain/gantry/GantryCouplingController.h"
#include "domain/gantry/GantryPowerController.h"
#include "infrastructure/ISystemDriver.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/logger/TraceScope.h"

GantryViewModel::GantryViewModel(SystemManager& manager, const std::string& groupName,
                                 QObject* parent)
    : QObject(parent)
    , m_manager(manager)
    , m_groupName(groupName)
{
    LOG_INFO(LogLayer::UI, "GantryVM",
        m_groupName + " GantryViewModel created");
}

GantryViewModel::~GantryViewModel() {
    LOG_DEBUG(LogLayer::UI, "GantryVM",
        m_groupName + " GantryViewModel destroyed");
}

// ========== 状态投影查询 ==========

bool GantryViewModel::isEnabled() const {
    return m_cachedEnabled;
}

bool GantryViewModel::isCoupled() const {
    return m_cachedCoupled;
}

bool GantryViewModel::isDecoupledAndEnabled() const {
    return m_cachedDecoupledAndEnabled;
}

bool GantryViewModel::isSynchronized() const {
    return m_cachedSynchronized;
}

// ========== 编排器状态查询 ==========

bool GantryViewModel::isOrchestratorBusy() const {
    return m_cachedOrchestratorBusy;
}

QString GantryViewModel::orchestratorStepText() const {
    return m_cachedOrchestratorStepText;
}

// ========== 操作入口（Q_INVOKABLE） ==========

void GantryViewModel::startCoupling() {
    TraceScope scope(m_groupName, "Gantry", generateTraceId());
    LOG_INFO(LogLayer::UI, "GantryVM",
        m_groupName + " startCoupling requested");

    // 释放已完成的 orchestrator
    if (m_orchestrator && m_orchestrator->isDone()) {
        LOG_DEBUG(LogLayer::UI, "GantryVM",
            m_groupName + " startCoupling: releasing previous completed orchestrator");
        m_orchestrator.reset();
    }

    // 如果 orchestrator 尚在运行，忽略重复触发
    if (m_orchestrator && !m_orchestrator->isDone() && !m_orchestrator->hasError()) {
        LOG_DEBUG(LogLayer::UI, "GantryVM",
            m_groupName + " startCoupling: orchestrator still running, ignored");
        return;
    }

    // 创建新的 orchestrator 并启动联动
    m_orchestrator = std::make_unique<GantryOrchestrator>(m_manager, m_groupName);
    m_orchestrator->startCoupling();
    LOG_DEBUG(LogLayer::UI, "GantryVM",
        m_groupName + " startCoupling: orchestrator created and started");
}

void GantryViewModel::stopCouplingAndDisable() {
    TraceScope scope(m_groupName, "Gantry", generateTraceId());
    LOG_INFO(LogLayer::UI, "GantryVM",
        m_groupName + " stopCouplingAndDisable requested");

    // 释放已完成的 orchestrator
    if (m_orchestrator && m_orchestrator->isDone()) {
        LOG_DEBUG(LogLayer::UI, "GantryVM",
            m_groupName + " stopCouplingAndDisable: releasing previous completed orchestrator");
        m_orchestrator.reset();
    }

    if (m_orchestrator && !m_orchestrator->isDone() && !m_orchestrator->hasError()) {
        LOG_DEBUG(LogLayer::UI, "GantryVM",
            m_groupName + " stopCouplingAndDisable: orchestrator still running, ignored");
        return;
    }

    m_orchestrator = std::make_unique<GantryOrchestrator>(m_manager, m_groupName);
    m_orchestrator->stopCouplingAndDisable();
    LOG_DEBUG(LogLayer::UI, "GantryVM",
        m_groupName + " stopCouplingAndDisable: orchestrator created and started");
}

void GantryViewModel::enableAndDecouple() {
    TraceScope scope(m_groupName, "Gantry", generateTraceId());
    LOG_INFO(LogLayer::UI, "GantryVM",
        m_groupName + " enableAndDecouple requested");

    if (m_orchestrator && m_orchestrator->isDone()) {
        LOG_DEBUG(LogLayer::UI, "GantryVM",
            m_groupName + " enableAndDecouple: releasing previous completed orchestrator");
        m_orchestrator.reset();
    }

    if (m_orchestrator && !m_orchestrator->isDone() && !m_orchestrator->hasError()) {
        LOG_DEBUG(LogLayer::UI, "GantryVM",
            m_groupName + " enableAndDecouple: orchestrator still running, ignored");
        return;
    }

    m_orchestrator = std::make_unique<GantryOrchestrator>(m_manager, m_groupName);
    m_orchestrator->enableAndDecouple();
    LOG_DEBUG(LogLayer::UI, "GantryVM",
        m_groupName + " enableAndDecouple: orchestrator created and started");
}

void GantryViewModel::enable() {
    TraceScope scope(m_groupName, "Gantry", generateTraceId());
    LOG_INFO(LogLayer::UI, "GantryVM",
        m_groupName + " enable requested");

    auto* ctx = getContext();
    if (!ctx) {
        LOG_ERROR(LogLayer::UI, "GantryVM",
            m_groupName + " enable failed: context not found");
        return;
    }

    auto& power = ctx->gantryPowerController();
    auto result = power.requestEnable(true);
    if (result != GantryRejection::None) {
        LOG_WARN(LogLayer::UI, "GantryVM",
            m_groupName + " enable rejected: " + rejectionToString(result));
    } else {
        LOG_DEBUG(LogLayer::UI, "GantryVM",
            m_groupName + " enable accepted, pending="
            + std::to_string(power.hasPendingCommand()));
    }
}

void GantryViewModel::disable() {
    TraceScope scope(m_groupName, "Gantry", generateTraceId());
    LOG_INFO(LogLayer::UI, "GantryVM",
        m_groupName + " disable requested");

    auto* ctx = getContext();
    if (!ctx) {
        LOG_ERROR(LogLayer::UI, "GantryVM",
            m_groupName + " disable failed: context not found");
        return;
    }

    auto& power = ctx->gantryPowerController();
    auto result = power.requestEnable(false);
    if (result != GantryRejection::None) {
        LOG_WARN(LogLayer::UI, "GantryVM",
            m_groupName + " disable rejected: " + rejectionToString(result));
    } else {
        LOG_DEBUG(LogLayer::UI, "GantryVM",
            m_groupName + " disable accepted, pending="
            + std::to_string(power.hasPendingCommand()));
    }
}

bool GantryViewModel::verifyPassword(const QString& password) const {
    bool ok = password == QString::fromLatin1(PASSWORD);
    LOG_DEBUG(LogLayer::UI, "GantryVM",
        m_groupName + " verifyPassword: " + (ok ? "OK" : "FAILED"));
    return ok;
}

// ========== 逐帧驱动 ==========

void GantryViewModel::tick() {
    advanceOrchestrator();
    refreshGantryState();
    refreshOrchestratorState();

    LOG_TRACE_EVERY_N(100, LogLayer::UI, "GantryVM",
        m_groupName + " tick: enabled=" + std::to_string(m_cachedEnabled)
        + " coupled=" + std::to_string(m_cachedCoupled)
        + " sync=" + std::to_string(m_cachedSynchronized)
        + " orchBusy=" + std::to_string(m_cachedOrchestratorBusy));
}

// ========== 私有方法 ==========

SystemContext* GantryViewModel::getContext() {
    SystemContext* ctx = nullptr;
    ContextRejection reason;
    if (!m_manager.tryGetGroup(m_groupName, ctx, reason)) {
        return nullptr;
    }
    return ctx;
}

void GantryViewModel::refreshGantryState() {
    auto* ctx = getContext();
    if (!ctx) {
        // 无法获取上下文：所有状态归零
        bool changed = false;
        if (m_cachedEnabled) { m_cachedEnabled = false; changed = true; }
        if (m_cachedCoupled) { m_cachedCoupled = false; changed = true; }
        if (m_cachedDecoupledAndEnabled) { m_cachedDecoupledAndEnabled = false; changed = true; }
        if (m_cachedSynchronized) { m_cachedSynchronized = false; changed = true; }
        if (changed) {
            LOG_DEBUG(LogLayer::UI, "GantryVM",
                m_groupName + " refreshGantryState: context lost, all states reset to false");
            emit gantryStateChanged();
        }
        return;
    }

    auto& power = ctx->gantryPowerController();
    auto& coupling = ctx->gantryCouplingController();

    bool enabled = power.isEnabled();
    bool coupled = coupling.isCoupled();
    bool synchronized = power.isSynchronized() && !coupling.isNotSynchronized();
    bool decoupledAndEnabled = enabled && !coupled && synchronized;

    bool changed = false;

    if (m_cachedEnabled != enabled) {
        LOG_DEBUG(LogLayer::UI, "GantryVM",
            m_groupName + " enabled: " + std::to_string(m_cachedEnabled) + " -> " + std::to_string(enabled));
        m_cachedEnabled = enabled;
        changed = true;
    }
    if (m_cachedCoupled != coupled) {
        LOG_DEBUG(LogLayer::UI, "GantryVM",
            m_groupName + " coupled: " + std::to_string(m_cachedCoupled) + " -> " + std::to_string(coupled));
        m_cachedCoupled = coupled;
        changed = true;
    }
    if (m_cachedDecoupledAndEnabled != decoupledAndEnabled) {
        m_cachedDecoupledAndEnabled = decoupledAndEnabled;
        changed = true;
    }
    if (m_cachedSynchronized != synchronized) {
        LOG_DEBUG(LogLayer::UI, "GantryVM",
            m_groupName + " synchronized: " + std::to_string(m_cachedSynchronized) + " -> " + std::to_string(synchronized));
        m_cachedSynchronized = synchronized;
        changed = true;
    }

    if (changed) {
        emit gantryStateChanged();
    }
}

void GantryViewModel::refreshOrchestratorState() {
    bool busy = false;
    int step = -1;  // -1 表示无 orchestrator

    if (m_orchestrator) {
        auto s = m_orchestrator->currentStep();
        step = static_cast<int>(s);

        // 忙碌状态：非终态（Done / Error）且非 Initial
        if (s != GantryOrchestrator::Step::Done
            && s != GantryOrchestrator::Step::Error
            && s != GantryOrchestrator::Step::Initial) {
            busy = true;
        }
    }

    QString stepText = stepToText(step);

    bool changed = false;
    if (m_cachedOrchestratorBusy != busy) {
        LOG_DEBUG(LogLayer::UI, "GantryVM",
            m_groupName + " orchBusy: " + std::to_string(m_cachedOrchestratorBusy) + " -> " + std::to_string(busy));
        m_cachedOrchestratorBusy = busy;
        changed = true;
    }
    if (m_cachedOrchestratorStepText != stepText) {
        LOG_DEBUG(LogLayer::UI, "GantryVM",
            m_groupName + " orchStep: " + m_cachedOrchestratorStepText.toStdString()
            + " -> " + stepText.toStdString());
        m_cachedOrchestratorStepText = stepText;
        changed = true;
    }

    if (changed) {
        emit orchestratorStateChanged();
    }
}

void GantryViewModel::advanceOrchestrator() {
    if (!m_orchestrator) return;

    // 终态不再推进
    if (m_orchestrator->isDone()) {
        LOG_TRACE_EVERY_N(50, LogLayer::UI, "GantryVM",
            m_groupName + " advanceOrchestrator: orchestrator done");
        return;
    }
    if (m_orchestrator->hasError()) {
        LOG_TRACE_EVERY_N(50, LogLayer::UI, "GantryVM",
            m_groupName + " advanceOrchestrator: orchestrator has error, stopped");
        return;
    }

    m_orchestrator->tick();
}

QString GantryViewModel::stepToText(int step) {
    switch (step) {
        case -1:                          return QStringLiteral("Ready");
        case static_cast<int>(GantryOrchestrator::Step::Initial):          return QStringLiteral("Standby");
        case static_cast<int>(GantryOrchestrator::Step::EnsuringEnabled):  return QStringLiteral("Enabling gantry motors...");
        case static_cast<int>(GantryOrchestrator::Step::WaitingEnabled):   return QStringLiteral("Waiting for motors enable...");
        case static_cast<int>(GantryOrchestrator::Step::Coupling):         return QStringLiteral("Coupling...");
        case static_cast<int>(GantryOrchestrator::Step::WaitingCoupled):   return QStringLiteral("Waiting for coupled confirmation...");
        case static_cast<int>(GantryOrchestrator::Step::Decoupling):       return QStringLiteral("Decoupling...");
        case static_cast<int>(GantryOrchestrator::Step::WaitingDecoupled): return QStringLiteral("Waiting for decoupled confirmation...");
        case static_cast<int>(GantryOrchestrator::Step::Disabling):        return QStringLiteral("Disabling gantry motors...");
        case static_cast<int>(GantryOrchestrator::Step::WaitingDisabled):  return QStringLiteral("Waiting for motors disable...");
        case static_cast<int>(GantryOrchestrator::Step::Done):             return QStringLiteral("Done");
        case static_cast<int>(GantryOrchestrator::Step::Error):            return QStringLiteral("Error");
        default:                           return QStringLiteral("Unknown");
    }
}

/// @brief 生成 TraceScope 的唯一 traceId（基于纳秒时间戳 + 原子计数器）
std::string GantryViewModel::generateTraceId() {
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::steady_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    return "Gantry_" + std::to_string(ns) + "_" + std::to_string(++counter);
}
