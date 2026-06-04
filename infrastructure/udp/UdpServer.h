#pragma once

#include <QUdpSocket>
#include <QHostAddress>
#include <QByteArray>
#include <string>
#include <cstdint>

#include "application/SystemManager.h"
#include "application/udp/UdpCommandDispatcher.h"
#include "infrastructure/logger/Logger.h"

// ═══════════════════════════════════════════════════════════════════
// UDP 服务器 —— UDP 网络 IO 封装，不关心业务逻辑
// 参考: docs/architecture/UDP通讯层设计文档.md §3.3.1
//
// 职责：
//   1. 监听指定端口，非阻塞模式
//   2. 收包 → 透传给 UdpCommandDispatcher → 回包
//   3. 由外部 tick() 循环驱动（与 FakePLC/AxisSyncService 一致）
//
// 依赖方向：
//   UdpServer (infrastructure) → UdpCommandDispatcher (application)
// ═══════════════════════════════════════════════════════════════════

class UdpServer {
public:
    // ============================================================
    // 配置结构
    // ============================================================
    struct Config {
        uint16_t listenPort = 9001;          // 监听端口
        std::string bindAddress = "0.0.0.0"; // 绑定地址
        int recvBufferSize = 4096;           // 接收缓冲区大小（字节）
    };

    // ============================================================
    // 构造 / 析构
    // ============================================================

    explicit UdpServer(SystemManager& manager, const Config& cfg)
        : m_manager(manager)
        , m_dispatcher(manager)
        , m_config(cfg)
    {
    }

    ~UdpServer() {
        stop();
    }

    // ============================================================
    // 生命周期
    // ============================================================

    /// @brief 启动监听
    /// @return true 成功绑定端口，false 失败
    bool start() {
        if (m_running) {
            LOG_WARN(LogLayer::APP, "UdpServer", "Already running, ignoring start()");
            return true;
        }

        QHostAddress bindAddr(QString::fromStdString(m_config.bindAddress));
        if (!m_socket.bind(bindAddr, m_config.listenPort)) {
            LOG_ERROR(LogLayer::APP, "UdpServer",
                      "Failed to bind to " + m_config.bindAddress
                      + ":" + std::to_string(m_config.listenPort)
                      + " — " + m_socket.errorString().toStdString());
            return false;
        }

        m_running = true;
        LOG_INFO(LogLayer::APP, "UdpServer",
                 "Listening on " + m_config.bindAddress
                 + ":" + std::to_string(m_config.listenPort));
        return true;
    }

    /// @brief 停止监听
    void stop() {
        if (!m_running) return;
        m_socket.close();
        m_running = false;
        LOG_INFO(LogLayer::APP, "UdpServer", "Stopped");
    }

    // ============================================================
    // 逐帧驱动
    // ============================================================

    /// @brief 处理所有待处理数据报
    ///
    /// 每帧调用一次，由外部主循环驱动（与 FakePLC::tick、AxisSyncService::tick 并行）。
    ///
    /// 内部流程：
    ///   while (socket.hasPendingDatagrams())
    ///     ├── socket.readDatagram(data, &senderAddr, &senderPort)
    ///     ├── dispatchAndReply(data, senderAddr, senderPort)
    ///     │     ├── m_dispatcher.dispatch(jsonStr) → 返回 JSON 回复字符串
    ///     │     └── socket.writeDatagram(replyJson, senderAddr, senderPort)
    ///     └── end while
    void tick() {
        if (!m_running) return;

        while (m_socket.hasPendingDatagrams()) {
            // 预备接收缓冲区
            QByteArray buffer(m_config.recvBufferSize, '\0');
            QHostAddress senderAddr;
            uint16_t senderPort = 0;

            qint64 bytesRead = m_socket.readDatagram(
                buffer.data(), buffer.size(), &senderAddr, &senderPort);

            if (bytesRead <= 0) {
                LOG_WARN(LogLayer::APP, "UdpServer",
                         "readDatagram returned " + std::to_string(bytesRead)
                         + " — " + m_socket.errorString().toStdString());
                continue;
            }

            buffer.resize(static_cast<int>(bytesRead));
            std::string rawJson = buffer.toStdString();

            LOG_TRACE(LogLayer::APP, "UdpServer",
                      "Received " + std::to_string(bytesRead)
                      + " bytes from " + senderAddr.toString().toStdString()
                      + ":" + std::to_string(senderPort));

            // 透传给 Dispatcher 处理（不在此层解析 JSON）
            std::string replyJson = m_dispatcher.dispatch(rawJson);

            // 回包
            QByteArray replyData = QByteArray::fromStdString(replyJson);
            qint64 bytesSent = m_socket.writeDatagram(
                replyData, senderAddr, senderPort);

            if (bytesSent < 0) {
                LOG_ERROR(LogLayer::APP, "UdpServer",
                          "Failed to send reply to "
                          + senderAddr.toString().toStdString()
                          + ":" + std::to_string(senderPort)
                          + " — " + m_socket.errorString().toStdString());
            } else if (static_cast<size_t>(bytesSent) != replyJson.size()) {
                LOG_WARN(LogLayer::APP, "UdpServer",
                         "Partial send: " + std::to_string(bytesSent)
                         + "/" + std::to_string(replyJson.size()) + " bytes");
            }
        }
    }

    // ============================================================
    // 状态查询
    // ============================================================

    [[nodiscard]] bool isRunning() const { return m_running; }

private:
    SystemManager& m_manager;
    UdpCommandDispatcher m_dispatcher;
    Config m_config;
    QUdpSocket m_socket;
    bool m_running = false;
};