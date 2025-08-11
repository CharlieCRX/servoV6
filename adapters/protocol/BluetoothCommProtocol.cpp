// F:/project/servoV6/adapters/protocol/BluetoothCommProtocol.cpp
#include "BluetoothCommProtocol.h"
#include "Logger.h" // 包含日志头文件
#include <QBluetoothAddress>
#include <QBluetoothUuid>

// 蓝牙设备服务 UUID，你需要根据你的设备来确定
// 这里以一个通用的串口服务UUID为例
const QBluetoothUuid serviceUuid(QBluetoothUuid::ServiceClassUuid::SerialPort);


BluetoothCommProtocol::BluetoothCommProtocol(QObject* parent)
    : QObject(parent)
    , m_socket(std::make_shared<QBluetoothSocket>(QBluetoothServiceInfo::RfcommProtocol))
    , m_discoveryAgent(std::make_shared<QBluetoothDeviceDiscoveryAgent>()) {
    // 连接 Qt 信号和槽
    connect(m_socket.get(), &QBluetoothSocket::connected, this, &BluetoothCommProtocol::onConnected);
    connect(m_socket.get(), &QBluetoothSocket::disconnected, this, &BluetoothCommProtocol::onDisconnected);
    connect(m_socket.get(), &QBluetoothSocket::readyRead, this, &BluetoothCommProtocol::onReadyRead);
    connect(m_socket.get(), QOverload<QBluetoothSocket::SocketError>::of(&QBluetoothSocket::errorOccurred),
            this, &BluetoothCommProtocol::onErrorOccurred);

    // 连接设备发现代理的信号
    connect(m_discoveryAgent.get(), &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BluetoothCommProtocol::deviceDiscovered);
    connect(m_discoveryAgent.get(), &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BluetoothCommProtocol::deviceScanFinished);
}

BluetoothCommProtocol::~BluetoothCommProtocol() {
    close();
}

bool BluetoothCommProtocol::open(const std::string& deviceName, bool reOpen) {
    if (m_isOpened) {
        if (!reOpen) {
            LOG_WARN("蓝牙协议已打开，无需重复操作。");
            return true;
        }
        close();
    }

    // 如果指定了设备名称，则直接尝试连接
    if (!deviceName.empty()) {
        QBluetoothAddress address(QString::fromStdString(deviceName));
        if (address.isNull()) {
            LOG_ERROR("设备名称 '{}' 不是一个有效的蓝牙地址。", deviceName);
            return false;
        }
        LOG_INFO("尝试连接指定蓝牙设备：{}", deviceName);
        m_socket->connectToService(address, serviceUuid);
        m_isOpened = true; // 标志已发起连接
        return true;
    }

    // 如果没有指定设备名称，则开始扫描
    LOG_INFO("未指定设备，开始扫描设备名称以 '{}' 为前缀的蓝牙设备。", m_targetDeviceNamePrefix);
    m_discoveryAgent->start();
    return true; // 扫描操作已发起
}

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

bool BluetoothCommProtocol::isOpen() const {
    return m_isOpened && m_socket->isOpen();
}

bool BluetoothCommProtocol::read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out) {
    if (!isOpen()) {
        LOG_ERROR("蓝牙协议未打开，无法执行读操作。");
        return false;
    }

    // 协议实现：根据你的具体蓝牙通信协议来构造读命令
    // 这里仅为示例，你需要替换为实际的协议实现
    // 例如：将命令打包成字节数组
    QByteArray command;
    // 假设协议格式为：[命令头] [设备ID] [寄存器类型] [起始地址] [结束地址]
    command.append(0x01); // 读命令
    command.append(mID);
    command.append(regType);
    command.append(startReg);
    command.append(stopReg);

    qint64 bytesWritten = m_socket->write(command);
    if (bytesWritten == -1) {
        LOG_ERROR("写入蓝牙数据失败: {}", m_socket->errorString().toStdString());
        return false;
    }

    // 实际应用中，这里需要等待设备的响应，然后解析数据填充到 out
    // 这通常是一个异步过程，数据会在 onReadyRead 槽中接收
    // 这里我们先返回 true，表示命令已发送
    LOG_DEBUG("已发送读命令到设备 {}，寄存器类型 {}，地址 {}-{}", mID, regType, startReg, stopReg);

    return true;
}

bool BluetoothCommProtocol::write(int mID, int regType, int reg, const RegisterBlock& in) {
    if (!isOpen()) {
        LOG_ERROR("蓝牙协议未打开，无法执行写操作。");
        return false;
    }

    // 协议实现：根据你的具体蓝牙通信协议来构造写命令
    // 例如：将命令和数据打包成字节数组
    QByteArray command;
    // 假设协议格式为：[命令头] [设备ID] [寄存器类型] [地址] [数据]
    command.append(0x02); // 写命令
    command.append(mID);
    command.append(regType);
    command.append(reg);
    for (uint16_t data : in.data) {
        // 将 uint16_t 转换为字节，注意大小端
        command.append(static_cast<char>((data >> 8) & 0xFF));
        command.append(static_cast<char>(data & 0xFF));
    }

    qint64 bytesWritten = m_socket->write(command);
    if (bytesWritten == -1) {
        LOG_ERROR("写入蓝牙数据失败: {}", m_socket->errorString().toStdString());
        return false;
    }

    LOG_DEBUG("已发送写命令到设备 {}，寄存器类型 {}，地址 {}，数据大小 {}", mID, regType, reg, in.data.size());

    return true;
}

void BluetoothCommProtocol::onConnected() {
    m_isOpened = true;
    LOG_INFO("成功连接到蓝牙设备: {}", m_socket->peerName().toStdString());
}

void BluetoothCommProtocol::onDisconnected() {
    m_isOpened = false;
    LOG_INFO("蓝牙设备已断开连接。");
}

void BluetoothCommProtocol::onReadyRead() {
    // 接收并处理来自蓝牙设备的数据
    QByteArray data = m_socket->readAll();
    LOG_DEBUG("收到 {} 字节数据：{}", data.size(), data.toHex().toStdString());

    // 这里需要解析接收到的数据，并根据你的协议进行处理
    // 例如，将数据存储在一个缓冲区中，等待完整的响应包，然后进行解析
    // 并在 read 方法的回调中填充 RegisterBlock
}

void BluetoothCommProtocol::onErrorOccurred(QBluetoothSocket::SocketError error) {
    LOG_ERROR("蓝牙套接字发生错误: {}", m_socket->errorString().toStdString());
}

// 新增槽函数：当发现设备时调用
void BluetoothCommProtocol::deviceDiscovered(const QBluetoothDeviceInfo &device) {
    LOG_DEBUG("发现设备：{} ({})", device.name().toStdString(), device.address().toString().toStdString());

    // 检查设备名称是否以指定前缀开头
    if (device.name().startsWith(QString::fromStdString(m_targetDeviceNamePrefix))) {
        LOG_INFO("找到目标设备 '{}'，地址：{}。停止扫描并尝试连接。",
                 device.name().toStdString(), device.address().toString().toStdString());

        m_discoveryAgent->stop(); // 找到目标设备后停止扫描
        m_socket->connectToService(device.address(), serviceUuid);
        m_isOpened = true; // 标志已发起连接
    }
}

// 新增槽函数：当扫描完成时调用
void BluetoothCommProtocol::deviceScanFinished() {
    if (!m_isOpened) {
        LOG_ERROR("设备扫描完成，但未找到匹配的设备。无法建立连接。");
    }
}
