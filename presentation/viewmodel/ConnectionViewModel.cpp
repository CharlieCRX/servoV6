#include "ConnectionViewModel.h"

#include "application/SystemManager.h"
#include "domain/entity/SystemContext.h"
#include "infrastructure/ISystemDriver.h"
#include "infrastructure/logger/Logger.h"

ConnectionViewModel::ConnectionViewModel(SystemManager& manager, const std::string& groupName,
                                         QObject* parent)
    : QObject(parent)
    , m_manager(manager)
    , m_groupName(groupName)
{
    LOG_INFO(LogLayer::UI, "ConnectionViewModel",
        "ConnectionViewModel created for group: " + groupName);
}

ISystemDriver* ConnectionViewModel::getDriver()
{
    SystemContext* ctx = nullptr;
    ContextRejection reason;
    if (!m_manager.tryGetGroup(m_groupName, ctx, reason) || !ctx) {
        return nullptr;
    }
    return ctx->driver();
}

bool ConnectionViewModel::isConnected() const
{
    return m_cachedConnected;
}

QString ConnectionViewModel::statusText() const
{
    return m_cachedStatusText;
}

void ConnectionViewModel::reconnect()
{
    ISystemDriver* drv = getDriver();
    if (!drv) {
        LOG_WARN(LogLayer::UI, "ConnectionViewModel",
            "reconnect() -- no driver available for group: " + m_groupName);
        return;
    }

    LOG_INFO(LogLayer::UI, "ConnectionViewModel",
        "reconnect() -- triggering manual reconnect for group: " + m_groupName);
    drv->reconnect();
}

void ConnectionViewModel::tick()
{
    ISystemDriver* drv = getDriver();
    if (!drv) {
        // 驱动不可用 → 标记为未知
        if (m_cachedConnected || m_cachedStatusText != QStringLiteral("未知: 驱动未初始化")) {
            m_cachedConnected = false;
            m_cachedStatusText = QStringLiteral("未知: 驱动未初始化");
            emit connectionChanged();
        }
        return;
    }

    ConnectionState state = drv->getConnectionState();
    bool newConnected = state.connected;
    QString newStatusText = state.connected
        ? QStringLiteral("已连接")
        : QStringLiteral("断连");

    if (!state.diagnostic.empty()) {
        if (state.connected) {
            newStatusText += " (" + QString::fromStdString(state.diagnostic) + ")";
        } else {
            // 断连时优先显示诊断信息
            newStatusText = QString::fromStdString(state.diagnostic);
        }
    }

    if (newConnected != m_cachedConnected || newStatusText != m_cachedStatusText) {
        m_cachedConnected = newConnected;
        m_cachedStatusText = newStatusText;

        LOG_INFO(LogLayer::UI, "ConnectionViewModel",
            "Connection state changed for [" + m_groupName + "]: connected="
            + (m_cachedConnected ? "true" : "false")
            + " statusText=" + m_cachedStatusText.toStdString());

        emit connectionChanged();
    }
}