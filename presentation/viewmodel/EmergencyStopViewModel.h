#ifndef EMERGENCY_STOP_VIEW_MODEL_H
#define EMERGENCY_STOP_VIEW_MODEL_H

#include <QObject>
#include <QString>
#include "application/SystemManager.h"
#include "application/safety/EmergencyStopUseCase.h"
#include "application/safety/ReleaseEmergencyStopUseCase.h"
#include "domain/entity/SystemContext.h"
#include "domain/safety/EmergencyStopController.h"
#include "domain/safety/SafetyState.h"

/**
 * @brief 急停 ViewModel — UI ↔ 安全域的桥梁
 *
 * 职责：
 *   1. 向 QML 暴露分组级安全状态（SafetyState → Q_PROPERTY）
 *   2. 接收 UI 指令 → 调用 EmergencyStopUseCase / ReleaseEmergencyStopUseCase
 *   3. 每帧 tick() 从 EmergencyStopController 读取最新状态并通知 UI
 *
 * 设计原则：
 *   - 一个实例对应一个分组（Machine_A / Machine_B）
 *   - 无状态代理：状态源是 EmergencyStopController，ViewModel 只做桥接
 *   - 错误透明传递：UseCase 返回的 UseCaseError 通过 lastError 属性暴露
 */
class EmergencyStopViewModel : public QObject {
    Q_OBJECT

    // ──────────────── 状态投影 ────────────────
    /// @brief 安全状态枚举值：0=NotSynchronized, 1=Running, 2=EmergencyStopping, 3=EmergencyStopped, 4=ReleasingEmergencyStop
    Q_PROPERTY(int safetyState READ safetyState NOTIFY safetyStateChanged)

    /// @brief 系统是否处于锁定状态（禁止一切运动和使能）
    Q_PROPERTY(bool isSystemLocked READ isSystemLocked NOTIFY safetyStateChanged)

    /// @brief 是否处于已急停锁定（PLC 已确认）
    Q_PROPERTY(bool isEmergencyStopped READ isEmergencyStopped NOTIFY safetyStateChanged)

    /// @brief 是否正在等待 PLC 反馈（过渡中）
    Q_PROPERTY(bool isTransitioning READ isTransitioning NOTIFY safetyStateChanged)

    /// @brief 是否尚未与 PLC 同步（启动初态）
    Q_PROPERTY(bool isNotSynchronized READ isNotSynchronized NOTIFY safetyStateChanged)

    /// @brief 当前安全状态的文本描述
    Q_PROPERTY(QString safetyStateText READ safetyStateText NOTIFY safetyStateChanged)

    /// @brief 操作反馈文本（成功为空，失败为错误描述）
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    /**
     * @brief 构造急停 ViewModel
     * @param manager 系统分组管理器
     * @param groupName 目标分组名称（如 "Machine_A"）
     * @param parent Qt 父对象
     */
    explicit EmergencyStopViewModel(SystemManager& manager,
                                     const std::string& groupName,
                                     QObject* parent = nullptr)
        : QObject(parent)
        , m_manager(manager)
        , m_groupName(groupName)
    {
    }

    // ──────────────── Getters ────────────────

    int safetyState() const { return static_cast<int>(m_cachedState); }
    bool isSystemLocked() const { return m_cachedLocked; }
    bool isEmergencyStopped() const { return m_cachedEmergencyStopped; }
    bool isTransitioning() const { return m_cachedTransitioning; }
    bool isNotSynchronized() const { return m_cachedNotSynchronized; }
    QString lastError() const { return m_lastError; }

    QString safetyStateText() const {
        switch (m_cachedState) {
        case SafetyState::NotSynchronized:        return QStringLiteral("同步中...");
        case SafetyState::Running:                return QStringLiteral("");
        case SafetyState::EmergencyStopping:      return QStringLiteral("急停处理中...");
        case SafetyState::EmergencyStopped:       return QStringLiteral("⚠ SYSTEM EMERGENCY STOPPED");
        case SafetyState::ReleasingEmergencyStop: return QStringLiteral("急停解除中...");
        default:                                  return QStringLiteral("未知状态");
        }
    }

    // ──────────────── 急停操作 ────────────────

    /**
     * @brief 触发紧急急停
     *
     * 调用链：EmergencyStopUseCase.execute(m_manager, m_groupName)
     *   → requestEmergencyStop() → send(EmergencyStopCommand{true})
     *
     * 成功时 lastError 被清空；失败时写入错误描述。
     * 发送命令后，状态变更由下一帧 feedback loop 完成。
     */
    Q_INVOKABLE void triggerEmergencyStop() {
        EmergencyStopUseCase uc;
        auto result = uc.execute(m_manager, m_groupName);

        if (std::holds_alternative<std::monostate>(result)) {
            setLastError({});
        } else {
            setLastError(formatError(result));
        }
    }

    /**
     * @brief 解除紧急急停
     *
     * 前置条件：EmergencyStopController 必须处于 EmergencyStopped
     * 调用链：ReleaseEmergencyStopUseCase.execute(m_manager, m_groupName)
     *   → requestReleaseEmergencyStop() → send(EmergencyStopCommand{false})
     */
    Q_INVOKABLE void releaseEmergencyStop() {
        ReleaseEmergencyStopUseCase uc;
        auto result = uc.execute(m_manager, m_groupName);

        if (std::holds_alternative<std::monostate>(result)) {
            setLastError({});
        } else {
            setLastError(formatError(result));
        }
    }

    // ──────────────── 帧驱动 ────────────────

    /**
     * @brief 每帧从 EmergencyStopController 读取最新状态并发射变更信号
     *
     * 应在全局 tick loop 中调用（与 AxisViewModel::tick() 同级）
     */
    void tick() {
        SystemContext* ctx = nullptr;
        ContextRejection reason = ContextRejection::None;
        if (!m_manager.tryGetGroup(m_groupName, ctx, reason) || !ctx) {
            return;  // 分组不存在或无效，静默跳过
        }

        auto& controller = ctx->emergencyStopController();
        SafetyState newState = controller.state();
        bool newLocked              = controller.isSystemLocked();
        bool newEmergencyStopped    = controller.isEmergencyStopped();
        bool newTransitioning       = controller.isTransitioning();
        bool newNotSynchronized     = controller.isNotSynchronized();

        bool changed =
            (newState             != m_cachedState)             ||
            (newLocked            != m_cachedLocked)            ||
            (newEmergencyStopped  != m_cachedEmergencyStopped)  ||
            (newTransitioning     != m_cachedTransitioning)     ||
            (newNotSynchronized   != m_cachedNotSynchronized);

        if (changed) {
            m_cachedState            = newState;
            m_cachedLocked           = newLocked;
            m_cachedEmergencyStopped = newEmergencyStopped;
            m_cachedTransitioning    = newTransitioning;
            m_cachedNotSynchronized  = newNotSynchronized;
            emit safetyStateChanged();
        }
    }

signals:
    void safetyStateChanged();
    void lastErrorChanged();

private:
    void setLastError(const QString& err) {
        if (m_lastError != err) {
            m_lastError = err;
            emit lastErrorChanged();
        }
    }

    /**
     * @brief 将 UseCaseError variant 转换为用户可读的错误消息
     */
    static QString formatError(const UseCaseError& error) {
        // SafetyRejection
        if (auto* sr = std::get_if<SafetyRejection>(&error)) {
            switch (*sr) {
            case SafetyRejection::None:                    return {};
            case SafetyRejection::NotSynchronized:         return QStringLiteral("系统尚未同步PLC状态，请等待同步完成");
            case SafetyRejection::SystemSafetyLocked:      return QStringLiteral("系统处于安全锁定状态");
            case SafetyRejection::AlreadyInState:          return QStringLiteral("系统已处于该状态，操作无效");
            case SafetyRejection::InvalidStateTransition:  return QStringLiteral("当前状态不允许此操作（正在解除急停中）");
            case SafetyRejection::NotEmergencyStopped:     return QStringLiteral("系统未处于急停状态，无需解除");
            }
        }
        // ContextRejection
        if (auto* cr = std::get_if<ContextRejection>(&error)) {
            switch (*cr) {
            case ContextRejection::GroupNotFound:          return QStringLiteral("分组不存在");
            case ContextRejection::GroupNameInvalid:       return QStringLiteral("分组名称无效");
            case ContextRejection::GroupAlreadyExists:     return QStringLiteral("分组已存在");
            default:                                       return QStringLiteral("分组层拒绝操作");
            }
        }
        // CommunicationResult
        if (auto* comm = std::get_if<CommunicationResult>(&error)) {
            return QString::fromStdString("通讯失败: " + comm->diagnostic);
        }
        // Other unhandled types
        return QStringLiteral("操作失败（未知错误）");
    }

    // ──────────────── 成员 ────────────────
    SystemManager& m_manager;
    std::string    m_groupName;

    SafetyState m_cachedState              = SafetyState::NotSynchronized;
    bool        m_cachedLocked             = true;   // NotSynchronized 也算锁定
    bool        m_cachedEmergencyStopped   = false;
    bool        m_cachedTransitioning      = false;
    bool        m_cachedNotSynchronized    = true;

    QString m_lastError;
};

#endif // EMERGENCY_STOP_VIEW_MODEL_H
