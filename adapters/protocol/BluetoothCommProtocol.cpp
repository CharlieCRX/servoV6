// BluetoothCommProtocol.cpp

#include "BluetoothCommProtocol.h"
#include "Logger.h"
#include <QBluetoothAddress>
#include <QBluetoothUuid>
#include <QTimer>
#include <QEventLoop>

// 定义蓝牙设备的服务UUID。
// 这是一个通用的串口服务（SPP）示例。
// 如果你的设备使用不同的UUID，请修改此值。
const QBluetoothUuid serviceUuid(QBluetoothUuid::ServiceClassUuid::SerialPort);

/**
 * @brief 构造函数，创建 BluetoothCommProtocol 对象。
 * @param parent 父QObject对象。
 *
 * 初始化蓝牙套接字和设备发现代理，并连接所有必要的信号和槽。
 */
BluetoothCommProtocol::BluetoothCommProtocol(QObject* parent)
    : QObject(parent)
    , m_socket(std::make_shared<QBluetoothSocket>(QBluetoothServiceInfo::RfcommProtocol))
    , m_discoveryAgent(std::make_shared<QBluetoothDeviceDiscoveryAgent>()) {

    // 连接蓝牙套接字的信号到私有槽。
    connect(m_socket.get(), &QBluetoothSocket::connected, this, &BluetoothCommProtocol::onConnected);
    connect(m_socket.get(), &QBluetoothSocket::disconnected, this, &BluetoothCommProtocol::onDisconnected);
    connect(m_socket.get(), &QBluetoothSocket::readyRead, this, &BluetoothCommProtocol::onReadyRead);
    connect(m_socket.get(), QOverload<QBluetoothSocket::SocketError>::of(&QBluetoothSocket::errorOccurred),
            this, &BluetoothCommProtocol::onErrorOccurred);

    // 连接设备发现代理的信号到私有槽。
    connect(m_discoveryAgent.get(), &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BluetoothCommProtocol::deviceDiscovered);
    connect(m_discoveryAgent.get(), &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BluetoothCommProtocol::deviceScanFinished);
}

/**
 * @brief 析构函数，销毁 BluetoothCommProtocol 对象。
 *
 * 调用 close() 方法以确保所有资源都得到正确释放。
 */
BluetoothCommProtocol::~BluetoothCommProtocol() {
    close();
}

/**
 * @brief 打开与蓝牙设备的连接。
 * @param deviceName 设备的名称或MAC地址。
 * @param reOpen 强制重新打开连接的标志，即使它已打开。
 * @return 如果连接成功则返回 true，否则返回 false。
 *
 * 此方法处理两种情况：
 * 1. 如果提供了设备名称（MAC地址），则尝试直接连接。
 * 2. 如果未提供设备名称，则启动设备发现过程，以查找名称与 `m_targetDeviceNamePrefix` 匹配的设备。
 * 使用 QEventLoop 阻塞并等待连接或发现过程完成。
 */
bool BluetoothCommProtocol::open(const std::string& deviceName, bool reOpen) {
    if (m_isOpened) {
        if (!reOpen) {
            LOG_WARN("蓝牙协议已打开，无需重复操作。");
            return true;
        }
        close();
    }

    // 情况1: 使用提供的设备名称（MAC地址）直接连接。
    if (!deviceName.empty()) {
        QBluetoothAddress address(QString::fromStdString(deviceName));
        if (address.isNull()) {
            LOG_ERROR("设备地址 '{}' 无效，无法建立连接。", deviceName);
            return false;
        }
        LOG_INFO("尝试连接指定蓝牙设备：{}", deviceName);
        m_socket->connectToService(address, serviceUuid);
        m_isOpened = true; // 标记为已打开以等待连接状态。

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(m_socket.get(), &QBluetoothSocket::connected, &loop, &QEventLoop::quit);
        connect(m_socket.get(), QOverload<QBluetoothSocket::SocketError>::of(&QBluetoothSocket::errorOccurred), &loop, &QEventLoop::quit);
        connect(&timer, &QTimer::timeout, &loop, [&loop]() { loop.quit(); });
        timer.start(5000); // 5秒连接超时。
        loop.exec();

        if (m_socket->state() == QBluetoothSocket::SocketState::ConnectedState) {
            LOG_INFO("蓝牙连接成功。");
            emit connected();
            return true;
        } else {
            m_isOpened = false;
            LOG_ERROR("蓝牙连接失败: {}", m_socket->errorString().toStdString());
            return false;
        }
    }

    // 情况2: 未提供设备名称，启动设备发现。
    LOG_INFO("未指定设备名称，启动设备发现，搜索名称包含 '{}' 的设备。", m_targetDeviceNamePrefix);
    m_discoveryAgent->start();

    // 阻塞并等待发现完成或找到设备。
    QTimer scanTimer;
    scanTimer.setSingleShot(true);
    connect(m_discoveryAgent.get(), &QBluetoothDeviceDiscoveryAgent::finished, &m_discoveryLoop, &QEventLoop::quit);
    connect(&scanTimer, &QTimer::timeout, &m_discoveryLoop, &QEventLoop::quit);

    scanTimer.start(10000); // 10秒发现超时。
    m_discoveryLoop.exec();

    if (scanTimer.isActive()) {
        scanTimer.stop();
    }

    if (m_isOpened && m_socket->state() == QBluetoothSocket::SocketState::ConnectedState) {
        LOG_INFO("通过设备发现成功连接到设备。");
        return true;
    } else {
        LOG_ERROR("设备扫描完成，但未找到匹配的设备或连接失败。");
        m_isOpened = false;
        return false;
    }
}

/**
 * @brief 关闭蓝牙连接。
 *
 * 如果发现代理处于活动状态，则停止它并关闭套接字。
 */
void BluetoothCommProtocol::close() {
    if (m_discoveryAgent && m_discoveryAgent->isActive()) {
        m_discoveryAgent->stop();
    }
    if (m_socket && m_socket->isOpen()) {
        m_socket->close();
        LOG_INFO("蓝牙连接已关闭。");
    }
    m_isOpened = false;
}

/**
 * @brief 检查蓝牙连接是否打开。
 * @return 如果套接字已打开则返回 true，否则返回 false。
 */
bool BluetoothCommProtocol::isOpen() const {
    return m_isOpened && m_socket->isOpen();
}

// ---------------------- Modbus RTU 读写实现 ----------------------

/**
 * @brief 执行 Modbus RTU 读取操作。
 * @param mID 从设备的 Modbus ID。
 * @param regType 寄存器类型（例如，RelayStatus）。
 * @param startReg 起始寄存器地址。
 * @param stopReg 结束寄存器地址。
 * @param out 用于存储读取数据的 RegisterBlock。
 * @return 如果读取操作成功且响应有效则返回 true，否则返回 false。
 *
 * 构造一个 Modbus RTU 读取命令，发送它，并等待响应。
 * 使用 QEventLoop 处理阻塞等待响应。
 */
bool BluetoothCommProtocol::read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out) {
    if (!isOpen()) {
        LOG_ERROR("蓝牙协议未打开，无法执行读操作。");
        return false;
    }

    QByteArray command;
    // Modbus RTU 读取保持寄存器（功能码 0x03）
    command.append(static_cast<char>(mID));
    command.append(static_cast<char>(0x03)); // 功能码
    command.append(static_cast<char>((startReg >> 8) & 0xFF)); // 起始地址高字节
    command.append(static_cast<char>(startReg & 0xFF)); // 起始地址低字节
    quint16 count = stopReg - startReg + 1;
    command.append(static_cast<char>((count >> 8) & 0xFF)); // 寄存器数量高字节
    command.append(static_cast<char>(count & 0xFF)); // 寄存器数量低字节

    quint16 crc = calculateCRC(command);
    command.append(static_cast<char>(crc & 0xFF)); // CRC低字节
    command.append(static_cast<char>((crc >> 8) & 0xFF)); // CRC高字节

    qint64 bytesWritten = m_socket->write(command);
    if (bytesWritten == -1) {
        LOG_ERROR("写入蓝牙数据失败: {}", m_socket->errorString().toStdString());
        return false;
    }
    LOG_DEBUG("已发送读命令到设备 {}，寄存器类型 {}，地址 {}-{}, CRC={:04X}", mID, regType, startReg, stopReg, crc);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timeout = false;
    QByteArray responseData;

    connect(&timer, &QTimer::timeout, &loop, [&timeout, &loop]() {
        timeout = true;
        loop.quit();
    });

    // 连接 readyRead 信号以捕获响应。
    connect(m_socket.get(), &QBluetoothSocket::readyRead, this, [&loop, &responseData, this]() {
        responseData.append(m_socket->readAll());
        // 为了简化，我们假设已收到完整的响应并退出循环。
        // 在实际应用中，你需要检查 Modbus 帧是否完整。
        loop.quit();
    });

    timer.start(500); // 500ms 响应超时。
    loop.exec();

    if (timer.isActive()) {
        timer.stop();
    }

    // 断开临时连接以避免多次触发。
    disconnect(m_socket.get(), &QBluetoothSocket::readyRead, this, 0);

    if (timeout) {
        LOG_ERROR("Modbus读请求发送成功，但未收到回应或回应超时。");
        return false;
    }

    // 解析收到的响应。
    return parseModbusResponse(count, responseData, out);
}

/**
 * @brief 执行 Modbus RTU 单个寄存器写入操作。
 * @param mID 从设备的 Modbus ID。
 * @param regType 寄存器类型（例如，RelayStatus）。
 * @param reg 要写入的寄存器地址。
 * @param in 包含要写入数据的 RegisterBlock。
 * @return 如果写入操作成功且响应有效则返回 true，否则返回 false。
 *
 * 构造一个 Modbus RTU 写入命令，发送它，并等待响应。
 * 使用 QEventLoop 处理阻塞等待响应。
 */
bool BluetoothCommProtocol::write(int mID, int regType, int reg, const RegisterBlock& in) {
    if (!isOpen()) {
        LOG_ERROR("蓝牙协议未打开，无法执行写操作。");
        return false;
    }

    QByteArray command;
    // Modbus RTU 写入单个寄存器（功能码 0x06）
    command.append(static_cast<char>(mID));
    command.append(static_cast<char>(0x06)); // 功能码
    command.append(static_cast<char>((reg >> 8) & 0xFF)); // 寄存器地址高字节
    command.append(static_cast<char>(reg & 0xFF)); // 寄存器地址低字节

    if (in.data.size() != 1) {
        LOG_ERROR("写单个寄存器时数据块大小必须为1。");
        return false;
    }
    uint16_t value = in.data.front(); // 修复: first() -> front()
    command.append(static_cast<char>((value >> 8) & 0xFF)); // 数据值高字节
    command.append(static_cast<char>(value & 0xFF)); // 数据值低字节

    quint16 crc = calculateCRC(command);
    command.append(static_cast<char>(crc & 0xFF)); // CRC低字节
    command.append(static_cast<char>((crc >> 8) & 0xFF)); // CRC高字节

    qint64 bytesWritten = m_socket->write(command);
    if (bytesWritten == -1) {
        LOG_ERROR("写入蓝牙数据失败: {}", m_socket->errorString().toStdString());
        return false;
    }

    LOG_DEBUG("已发送写命令到设备 {}，寄存器类型 {}，地址 {}，数据：{}，CRC={:04X}",
              mID, regType, reg, QString::number(value, 16).toStdString(), crc);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timeout = false;
    QByteArray responseData;

    connect(&timer, &QTimer::timeout, &loop, [&timeout, &loop]() {
        timeout = true;
        loop.quit();
    });

    // 连接 readyRead 信号以捕获响应。
    connect(m_socket.get(), &QBluetoothSocket::readyRead, this, [&loop, &responseData, this]() {
        responseData.append(m_socket->readAll());
        // Modbus 写入单个寄存器响应固定为8字节。
        if (responseData.size() >= 8) {
            loop.quit();
        }
    });

    timer.start(500); // 500ms 响应超时。
    loop.exec();

    if (timer.isActive()) {
        timer.stop();
    }

    disconnect(m_socket.get(), &QBluetoothSocket::readyRead, this, 0);

    if (timeout) {
        LOG_ERROR("Modbus写请求发送成功，但未收到回应或回应超时。");
        return false;
    }

    // 使用CRC验证响应。
    QByteArray payload = responseData.left(responseData.size() - 2);
    quint16 calculatedCrc = calculateCRC(payload);
    quint16 receivedCrc = (static_cast<quint8>(responseData.at(responseData.size() - 1)) << 8) |
                          static_cast<quint8>(responseData.at(responseData.size() - 2));

    if (calculatedCrc != receivedCrc) {
        LOG_ERROR("Modbus写回应CRC校验失败！计算值: 0x{:04X}, 实际值: 0x{:04X}", calculatedCrc, receivedCrc);
        return false;
    }

    LOG_INFO("Modbus写命令成功，收到有效回应。");
    return true;
}

// ---------------------- 私有槽函数 ----------------------

/**
 * @brief QBluetoothSocket 的 connected() 信号槽。
 */
void BluetoothCommProtocol::onConnected() {
    LOG_INFO("成功连接到蓝牙设备: {}", m_socket->peerName().toStdString());
    if (m_discoveryAgent && m_discoveryAgent->isActive()) {
        m_discoveryAgent->stop();
    }
    emit connected();
}

/**
 * @brief QBluetoothSocket 的 disconnected() 信号槽。
 */
void BluetoothCommProtocol::onDisconnected() {
    m_isOpened = false;
    LOG_INFO("蓝牙设备已断开连接。");
    emit disconnected();
}

/**
 * @brief QBluetoothSocket 的 readyRead() 信号槽。
 *
 * 此槽主要用于通用的非Modbus特定数据。
 * read/write 方法中的阻塞循环会处理其特定的响应。
 */
void BluetoothCommProtocol::onReadyRead() {
    // 此槽主要用于调试或处理后台数据流。
    // read/write 方法中的阻塞循环处理其特定响应。
    emit readyRead(m_socket->readAll());
}

/**
 * @brief QBluetoothSocket 的 errorOccurred() 信号槽。
 * @param error 套接字错误的类型。
 */
void BluetoothCommProtocol::onErrorOccurred(QBluetoothSocket::SocketError socketError) {
    LOG_ERROR("蓝牙套接字发生错误: {}", m_socket->errorString().toStdString());
    emit error(m_socket->errorString()); // 假设信号传字符串
}

/**
 * @brief QBluetoothDeviceDiscoveryAgent 的 deviceDiscovered() 信号槽。
 * @param device 发现的蓝牙设备信息。
 */
void BluetoothCommProtocol::deviceDiscovered(const QBluetoothDeviceInfo &device) {
    std::string name = device.name().toStdString();
    std::string addr = device.address().toString().toStdString();
    LOG_INFO("发现蓝牙设备：名称 '{}', 地址 '{}'", name, addr);
    m_scannedDeviceNames.push_back(name);

    if (device.name().startsWith(QString::fromStdString(m_targetDeviceNamePrefix))) {
        LOG_INFO("找到目标设备，尝试连接...");
        m_discoveryAgent->stop();
        m_socket->connectToService(device.address(), serviceUuid);
        m_discoveryLoop.quit();
    }
}

/**
 * @brief QBluetoothDeviceDiscoveryAgent 的 finished() 信号槽。
 */
void BluetoothCommProtocol::deviceScanFinished() {
    LOG_INFO("蓝牙设备扫描完成。");
    // 如果事件循环仍在运行，则退出它。
    m_discoveryLoop.quit();
}


// ---------------------- Modbus协议辅助函数 ----------------------

/**
 * @brief 为给定数据计算 Modbus RTU CRC16 校验和。
 * @param data 要计算CRC的数据。
 * @return 计算出的 CRC16 校验和。
 */
quint16 BluetoothCommProtocol::calculateCRC(const QByteArray& data) {
    quint16 crc = 0xFFFF;
    for (int i = 0; i < data.size(); ++i) {
        crc ^= static_cast<quint8>(data[i]);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief 解析 Modbus RTU 读取响应并进行验证。
 * @param expectedRegCount 响应中预期的寄存器数量。
 * @param data 从设备接收到的原始响应数据。
 * @param out 用于存储解析后数据的 RegisterBlock。
 * @return 如果响应有效则返回 true，否则返回 false。
 *
 * 在解析实际的寄存器值之前，执行CRC验证、检查功能码和验证数据长度。
 */
bool BluetoothCommProtocol::parseModbusResponse(int expectedRegCount, const QByteArray& data, RegisterBlock& out) {
    const int headerLen = 3; // 地址(1) + 功能码(1) + 字节数(1)
    const int crcLen = 2;
    const int expectedDataLen = expectedRegCount * 2;
    const int totalExpectedLen = headerLen + expectedDataLen + crcLen;

    if (data.size() < totalExpectedLen) {
        LOG_ERROR("Modbus回应报文过短，期待{}字节，实际{}字节。", totalExpectedLen, data.size());
        return false;
    }

    QByteArray payload = data.left(data.size() - crcLen);
    quint16 calculatedCrc = calculateCRC(payload);
    quint16 receivedCrc = (static_cast<quint8>(data.at(data.size() - 1)) << 8) |
                          static_cast<quint8>(data.at(data.size() - 2));

    if (calculatedCrc != receivedCrc) {
        LOG_ERROR("Modbus回应CRC校验失败！计算值: 0x{:04X}, 实际值: 0x{:04X}", calculatedCrc, receivedCrc);
        return false;
    }

    quint8 funcCode = static_cast<quint8>(data.at(1));
    if (funcCode != 0x03) {
        LOG_ERROR("Modbus回应功能码错误，期待0x03，实际0x{:02X}", funcCode);
        return false;
    }

    quint8 dataByteCount = static_cast<quint8>(data.at(2));
    if (dataByteCount != expectedDataLen) {
        LOG_ERROR("Modbus回应数据长度错误，期待{}字节，实际{}字节。", expectedDataLen, dataByteCount);
        return false;
    }

    out.data.clear();
    for (int i = 0; i < expectedRegCount; ++i) {
        int pos = 3 + i * 2;
        quint16 value = (static_cast<quint8>(data.at(pos)) << 8) | static_cast<quint8>(data.at(pos + 1));
        out.data.push_back(value);
    }

    LOG_INFO("Modbus读命令成功，收到并解析{}个寄存器数据。", out.data.size());
    return true;
}
