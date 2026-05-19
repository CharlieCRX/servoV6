#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <optional>

class SystemManager;
class GantryOrchestrator;
class SystemContext;

/**
 * @brief 龙门轴 ViewModel（QML 层 UI 状态投影）
 *
 * 职责：
 *   1. 将 GantryPowerController / GantryCouplingController 的领域状态
 *      投影为 Q_PROPERTY，供 QML 直接绑定。
 *   2. 派生属性 isDecoupledAndEnabled（Enabled + NotCoupled）标记"状态 C"
 *      —— 仅此状态允许密码验证后解耦（安全门控）。
 *   3. 密码验证 verifyPassword() —— 硬编码 123456（里程碑 A）。
 *   4. 封装 GantryOrchestrator 操作入口（startCoupling / stopCouplingAndDisable /
 *      enableAndDecouple / enable / disable）。
 *   5. tick() 驱动 orchestrator 推进 + 刷新缓存投影值 + 按需发射信号。
 *
 * 信号发射策略（与 QtAxisViewModel 一致）：
 *   - gantryStateChanged：仅当状态聚合哈希变化时 emit
 *   - orchestratorStateChanged：仅当 isOrchestratorBusy 或 stepText 变化时 emit
 *
 * 密码说明：
 *   硬编码 "123456" 为 Phase 2 里程碑 A 的 UI 安全门控密码。
 *   未来里程碑将迁移至硬件密钥或独立安全模块。
 *
 * 分层：
 *   本类不直接操作 Axis，不发送命令，不 poll 反馈。
 *   GantryOrchestrator 负责所有写操作。
 *   Domain 实体通过 SystemContext 获取，由外部 pollFeedback 刷新。
 */
class GantryViewModel : public QObject {
    Q_OBJECT

    // ========== 状态投影属性 ==========
    Q_PROPERTY(bool isEnabled READ isEnabled NOTIFY gantryStateChanged)
    Q_PROPERTY(bool isCoupled READ isCoupled NOTIFY gantryStateChanged)
    Q_PROPERTY(bool isDecoupledAndEnabled READ isDecoupledAndEnabled NOTIFY gantryStateChanged)
    Q_PROPERTY(bool isSynchronized READ isSynchronized NOTIFY gantryStateChanged)

    // ========== 编排器状态投影 ==========
    Q_PROPERTY(bool isOrchestratorBusy READ isOrchestratorBusy NOTIFY orchestratorStateChanged)
    Q_PROPERTY(QString orchestratorStepText READ orchestratorStepText NOTIFY orchestratorStateChanged)

public:
    /**
     * @param manager   系统管理器（用于获取 SystemContext、创建 Orchestrator）
     * @param groupName 目标分组名称
     */
    explicit GantryViewModel(SystemManager& manager, const std::string& groupName,
                             QObject* parent = nullptr);

    ~GantryViewModel() override;

    // ========== 状态投影查询 ==========

    bool isEnabled() const;
    bool isCoupled() const;
    bool isDecoupledAndEnabled() const;  ///< Enabled && !Coupled（状态 C）
    bool isSynchronized() const;         ///< PowerController + CouplingController 均已同步

    // ========== 编排器状态查询 ==========

    bool isOrchestratorBusy() const;
    QString orchestratorStepText() const;

    // ========== 操作入口（Q_INVOKABLE） ==========

    /// @brief 一键启动联动流程（使能 + 联动）
    Q_INVOKABLE void startCoupling();

    /// @brief 一键解耦 + 掉电（逆操作）
    Q_INVOKABLE void stopCouplingAndDisable();

    /// @brief 使能 + 解耦（保持电机使能，仅断联动，需密码校验）
    Q_INVOKABLE void enableAndDecouple();

    /// @brief 直接使能龙门电机（绕过 orchestrator）
    Q_INVOKABLE void enable();

    /// @brief 直接掉电龙门电机（绕过 orchestrator）
    Q_INVOKABLE void disable();

    /// @brief 密码验证（硬编码 123456）
    Q_INVOKABLE bool verifyPassword(const QString& password) const;

    // ========== 逐帧驱动 ==========

    /// @brief 驱动 orchestrator 推进 + 刷新缓存状态 + 按需发射信号
    void tick();

signals:
    /// @brief 状态聚合变化时发射（isEnabled / isCoupled / isDecoupledAndEnabled / isSynchronized）
    void gantryStateChanged();

    /// @brief orchestrator 状态变化时发射（isOrchestratorBusy / orchestratorStepText）
    void orchestratorStateChanged();

private:
    /// @brief 从 SystemManager 获取 SystemContext（内部辅助）
    SystemContext* getContext();

    /// @brief 刷新缓存投影值，若变化则 emit gantryStateChanged
    void refreshGantryState();

    /// @brief 刷新 orchestrator 投影值，若变化则 emit orchestratorStateChanged
    void refreshOrchestratorState();

    /// @brief 推进当前 orchestrator（如果存在）
    void advanceOrchestrator();

    /// @brief 将 Orchestrator::Step 翻译为 UI 可读文本
    static QString stepToText(int step);

private:
    SystemManager& m_manager;
    std::string m_groupName;

    std::unique_ptr<GantryOrchestrator> m_orchestrator;

    // ========== 缓存投影值（用于节流信号） ==========
    bool m_cachedEnabled = false;
    bool m_cachedCoupled = false;
    bool m_cachedDecoupledAndEnabled = false;
    bool m_cachedSynchronized = false;

    bool m_cachedOrchestratorBusy = false;
    QString m_cachedOrchestratorStepText;

    // ========== 常量 ==========
    static constexpr const char* PASSWORD = "123456";
};
