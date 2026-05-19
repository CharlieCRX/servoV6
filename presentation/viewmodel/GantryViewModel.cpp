#include "presentation/viewmodel/GantryViewModel.h"
#include "application/SystemManager.h"
#include "application/policy/GantryOrchestrator.h"
#include "domain/entity/SystemContext.h"
#include "domain/gantry/GantryCouplingController.h"
#include "domain/gantry/GantryPowerController.h"
#include "infrastructure/ISystemDriver.h"

GantryViewModel::GantryViewModel(SystemManager& manager, const std::string& groupName,
                                 QObject* parent)
    : QObject(parent)
    , m_manager(manager)
    , m_groupName(groupName)
{
}

GantryViewModel::~GantryViewModel() = default;

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
    // 释放已完成的 orchestrator
    if (m_orchestrator && m_orchestrator->isDone()) {
        m_orchestrator.reset();
    }

    // 如果 orchestrator 尚在运行，忽略重复触发
    if (m_orchestrator && !m_orchestrator->isDone() && !m_orchestrator->hasError()) {
        return;
    }

    // 创建新的 orchestrator 并启动联动
    m_orchestrator = std::make_unique<GantryOrchestrator>(m_manager, m_groupName);
    m_orchestrator->startCoupling();
}

void GantryViewModel::stopCouplingAndDisable() {
    // 释放已完成的 orchestrator
    if (m_orchestrator && m_orchestrator->isDone()) {
        m_orchestrator.reset();
    }

    if (m_orchestrator && !m_orchestrator->isDone() && !m_orchestrator->hasError()) {
        return;
    }

    m_orchestrator = std::make_unique<GantryOrchestrator>(m_manager, m_groupName);
    m_orchestrator->stopCouplingAndDisable();
}

void GantryViewModel::enableAndDecouple() {
    if (m_orchestrator && m_orchestrator->isDone()) {
        m_orchestrator.reset();
    }

    if (m_orchestrator && !m_orchestrator->isDone() && !m_orchestrator->hasError()) {
        return;
    }

    m_orchestrator = std::make_unique<GantryOrchestrator>(m_manager, m_groupName);
    m_orchestrator->enableAndDecouple();
}

void GantryViewModel::enable() {
    auto* ctx = getContext();
    if (!ctx) return;

    auto& power = ctx->gantryPowerController();
    power.requestEnable(true);
}

void GantryViewModel::disable() {
    auto* ctx = getContext();
    if (!ctx) return;

    auto& power = ctx->gantryPowerController();
    power.requestEnable(false);
}

bool GantryViewModel::verifyPassword(const QString& password) const {
    return password == QString::fromLatin1(PASSWORD);
}

// ========== 逐帧驱动 ==========

void GantryViewModel::tick() {
    advanceOrchestrator();
    refreshGantryState();
    refreshOrchestratorState();
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
        if (changed) emit gantryStateChanged();
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
        m_cachedEnabled = enabled;
        changed = true;
    }
    if (m_cachedCoupled != coupled) {
        m_cachedCoupled = coupled;
        changed = true;
    }
    if (m_cachedDecoupledAndEnabled != decoupledAndEnabled) {
        m_cachedDecoupledAndEnabled = decoupledAndEnabled;
        changed = true;
    }
    if (m_cachedSynchronized != synchronized) {
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
        m_cachedOrchestratorBusy = busy;
        changed = true;
    }
    if (m_cachedOrchestratorStepText != stepText) {
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
    if (m_orchestrator->isDone() || m_orchestrator->hasError()) {
        return;
    }

    m_orchestrator->tick();
}

QString GantryViewModel::stepToText(int step) {
    switch (step) {
        case -1:                          return QStringLiteral("就绪");
        case static_cast<int>(GantryOrchestrator::Step::Initial):          return QStringLiteral("待命");
        case static_cast<int>(GantryOrchestrator::Step::EnsuringEnabled):  return QStringLiteral("正在使能龙门电机…");
        case static_cast<int>(GantryOrchestrator::Step::WaitingEnabled):   return QStringLiteral("等待电机使能完成…");
        case static_cast<int>(GantryOrchestrator::Step::Coupling):         return QStringLiteral("正在建立联动…");
        case static_cast<int>(GantryOrchestrator::Step::WaitingCoupled):   return QStringLiteral("等待联动确认…");
        case static_cast<int>(GantryOrchestrator::Step::Decoupling):       return QStringLiteral("正在解除联动…");
        case static_cast<int>(GantryOrchestrator::Step::WaitingDecoupled): return QStringLiteral("等待解耦确认…");
        case static_cast<int>(GantryOrchestrator::Step::Disabling):        return QStringLiteral("正在关闭龙门电机…");
        case static_cast<int>(GantryOrchestrator::Step::WaitingDisabled):  return QStringLiteral("等待电机掉电完成…");
        case static_cast<int>(GantryOrchestrator::Step::Done):             return QStringLiteral("完成");
        case static_cast<int>(GantryOrchestrator::Step::Error):            return QStringLiteral("出错");
        default:                           return QStringLiteral("未知");
    }
}
