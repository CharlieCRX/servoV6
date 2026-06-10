#pragma once

#include <QObject>
#include <QString>
#include <string>

class SystemManager;
class SystemContext;
class ISystemDriver;

/**
 * @brief 连接状态 ViewModel（QML 层 TCP 连接状态投影）
 *
 * 职责：
 *   1. 通过 SystemManager → SystemContext → ISystemDriver 查询底层 TCP 连接状态
 *   2. 将 ConnectionState 投影为 Q_PROPERTY，供 QML 绑定
 *   3. 提供手动重连入口 reconnect()
 *   4. tick() 每帧刷新缓存投影值 + 按需发射信号
 *
 * 信号发射策略：
 *   - connectionChanged：仅当连接状态或状态文本变化时 emit
 *
 * 为何独立于 AxisViewModel：
 *   - 连接状态是"分组级别"概念，不属于任何一个轴
 *   - 一个分组只有一个 Modbus TCP 连接
 *   - 职责单一，不膨胀现有 ViewModel
 *
 * 分层：
 *   本类不直接操作 socket，不 poll 反馈。
 *   连接状态由基础设施层 AsioModbusTcpClient::m_connected (atomic) 维护，
 *   通过 ISystemDriver::getConnectionState() 透传到表现层。
 */
class ConnectionViewModel : public QObject {
    Q_OBJECT

    // ========== 连接状态投影属性 ==========
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY connectionChanged)

public:
    /**
     * @param manager   系统管理器（用于获取 SystemContext → ISystemDriver）
     * @param groupName 目标分组名称（如 "Machine_A"）
     */
    explicit ConnectionViewModel(SystemManager& manager, const std::string& groupName,
                                 QObject* parent = nullptr);

    ~ConnectionViewModel() override = default;

    // ========== 状态查询 ==========

    /// @brief 当前 TCP 是否已连接
    bool isConnected() const;

    /// @brief 状态文本（如 "已连接 192.168.1.88:502" 或 "断连"）
    QString statusText() const;

    // ========== 操作入口 ==========

    /// @brief 手动触发重连（跳过自动重连 2s 等待）
    Q_INVOKABLE void reconnect();

    // ========== 逐帧驱动 ==========

    /// @brief 刷新缓存状态 + 按需发射信号
    void tick();

signals:
    /// @brief 连接状态或状态文本变化时发射
    void connectionChanged();

private:
    /// @brief 从 SystemManager 获取 ISystemDriver（内部辅助）
    ISystemDriver* getDriver();

    SystemManager& m_manager;
    std::string m_groupName;

    // ========== 缓存投影值（用于节流信号） ==========
    bool m_cachedConnected = false;
    QString m_cachedStatusText;
};