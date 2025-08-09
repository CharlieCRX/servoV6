// 该文件实现了 BluetoothCommProtocol 类，它通过 Qt 的蓝牙模块将 C++ 的
// ICommProtocol 接口与 QML 界面连接起来。

#include "BluetoothCommProtocol.h"
#include <QEventLoop>
#include <QTimer>
#include <QBluetoothUuid>
#include <QThread>
#include <QDebug> // 用于日志记录

// 辅助函数：将 QmlRegisterBlock (QList<quint16>) 转换为 ICommProtocol 的 RegisterBlock (std::vector<uint16_t>)
static RegisterBlock qmlRegisterBlockToStd(const QmlRegisterBlock& qmlBlock) {
    RegisterBlock stdBlock;
    stdBlock.data.reserve(qmlBlock.data.size());
    for (quint16 val : qmlBlock.data) {
        stdBlock.data.push_back(static_cast<uint16_t>(val));
    }
    return stdBlock;
}

// 辅助函数：将 ICommProtocol 的 RegisterBlock 转换为 QmlRegisterBlock
static QmlRegisterBlock stdRegisterBlockToQml(const RegisterBlock& stdBlock) {
    QmlRegisterBlock qmlBlock;
    qmlBlock.data.reserve(stdBlock.data.size());
    for (uint16_t val : stdBlock.data) {
        qmlBlock.data.push_back(static_cast<quint16>(val));
    }
    return qmlBlock;
}

BluetoothCommProtocol::BluetoothCommProtocol(QObject* parent)
    : QObject(parent),
    socket_(nullptr),
    discoveryAgent_(nullptr),
    connectTimeoutMs_(5000),
    lastOpenResult_(false)
{
    qDebug() << "蓝牙通信协议：正在初始化...";
    socket_ = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);
    connect(socket_, &QBluetoothSocket::connected, this, &BluetoothCommProtocol::onSocketConnected);
    connect(socket_, &QBluetoothSocket::disconnected, this, &BluetoothCommProtocol::onSocketDisconnected);
    connect(socket_, &QBluetoothSocket::readyRead, this, &BluetoothCommProtocol::onSocketReadyRead);
    // 使用新的错误信号 `errorOccurred`，并捕获错误字符串
    connect(socket_, QOverload<QBluetoothSocket::SocketError>::of(&QBluetoothSocket::errorOccurred),
            this, [this](QBluetoothSocket::SocketError err){
                Q_UNUSED(err)
                emit errorOccured(socket_->errorString());
            });

    discoveryAgent_ = new QBluetoothDeviceDiscoveryAgent(this);
    connect(discoveryAgent_, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BluetoothCommProtocol::onDiscoveryDeviceDiscovered);
    connect(discoveryAgent_, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BluetoothCommProtocol::onDiscoveryFinished);
}

BluetoothCommProtocol::~BluetoothCommProtocol() {
    close();
    delete socket_;
    delete discoveryAgent_;
}

// =========================================================================
// ICommProtocol 接口实现
// =========================================================================

bool BluetoothCommProtocol::open(const std::string& deviceNameOrAddress, bool reOpen) {
    QString qNameOrAddress = QString::fromStdString(deviceNameOrAddress);

    if (isOpen()) {
        if (!reOpen) return true;
        close();
    }

    qDebug() << "蓝牙通信协议：正在尝试打开 [" << qNameOrAddress << "]";

    bool ok = scanAndConnectByNameOrAddress(qNameOrAddress);
    lastOpenResult_ = ok;

    if (ok) {
        qDebug() << "蓝牙通信协议：成功打开 [" << qNameOrAddress << "]";
        // 成功连接后，发送 connected() 信号
        emit connected();
    } else {
        qDebug() << "蓝牙通信协议：打开失败 [" << qNameOrAddress << "]";
    }
    return ok;
}

void BluetoothCommProtocol::close() {
    if (socket_ && socket_->isOpen()) {
        qDebug() << "蓝牙通信协议：正在关闭套接字...";
        socket_->close();
        connectedAddress_.clear();
        connectedName_.clear();
    }
}

bool BluetoothCommProtocol::isOpen() const {
    return socket_ && socket_->isOpen();
}

bool BluetoothCommProtocol::read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out) {
    if (!isOpen()) {
        qDebug() << "蓝牙通信协议：读取失败，连接未打开。";
        return false;
    }

    // 假设一个简单的读取协议，每个寄存器占用2个字节
    int count = stopReg - startReg + 1;
    QByteArray cmd;
    cmd.append(char(0x01)); // 假设的读取命令码
    cmd.append(char(mID & 0xFF));
    cmd.append(char(regType & 0xFF));
    cmd.append(char((startReg >> 8) & 0xFF));
    cmd.append(char(startReg & 0xFF));
    cmd.append(char((count >> 8) & 0xFF));
    cmd.append(char(count & 0xFF));

    // 写入命令并等待数据写入
    socket_->write(cmd);
    if (!socket_->waitForBytesWritten(2000)) {
        qDebug() << "蓝牙通信协议：等待写入数据失败";
        return false;
    }

    // 等待响应（简单的阻塞等待，带超时）
    int waitMs = 2000;
    int waited = 0;
    const int step = 50;
    while (waited < waitMs) {
        {
            std::lock_guard<std::mutex> guard(readMutex_);
            int needed = count * 2; // 需要的字节数
            if (readBuffer_.size() >= needed) {
                QByteArray payload = readBuffer_.left(needed);
                readBuffer_.remove(0, needed);
                out.data.clear();
                for (int i = 0; i < needed; i += 2) {
                    uint16_t v = (static_cast<uint8_t>(payload[i]) << 8) | static_cast<uint8_t>(payload[i+1]);
                    out.data.push_back(v);
                }
                return true;
            }
        }
        QThread::msleep(step);
        waited += step;
    }

    qDebug() << "蓝牙通信协议：读取超时";
    return false;
}

bool BluetoothCommProtocol::write(int mID, int regType, int reg, const RegisterBlock& in) {
    if (!isOpen()) {
        qDebug() << "蓝牙通信协议：写入失败，连接未打开。";
        return false;
    }

    // 假设一个简单的写入协议
    int count = static_cast<int>(in.data.size());
    QByteArray cmd;
    cmd.append(char(0x02)); // 假设的写入命令码
    cmd.append(char(mID & 0xFF));
    cmd.append(char(regType & 0xFF));
    cmd.append(char((reg >> 8) & 0xFF));
    cmd.append(char(reg & 0xFF));
    cmd.append(char((count >> 8) & 0xFF));
    cmd.append(char(count & 0xFF));
    for (auto v : in.data) {
        cmd.append(char((v >> 8) & 0xFF));
        cmd.append(char(v & 0xFF));
    }

    socket_->write(cmd);
    if (!socket_->waitForBytesWritten(3000)) {
        qDebug() << "蓝牙通信协议：等待写入数据失败";
        return false;
    }

    // 如果协议需要确认，可以等待应答；示例中直接返回 true
    return true;
}

// =========================================================================
// QML 适配层实现
// =========================================================================

bool BluetoothCommProtocol::openDevice(const QString& deviceNameOrAddress) {
    // 调用 ICommProtocol 的 open 方法
    return open(deviceNameOrAddress.toStdString());
}

bool BluetoothCommProtocol::readRegisters(int mID, int regType, int startReg, int stopReg, QmlRegisterBlock& out) {
    RegisterBlock stdOut;
    // 调用 ICommProtocol 的 read 方法
    bool success = read(mID, regType, startReg, stopReg, stdOut);
    if (success) {
        out = stdRegisterBlockToQml(stdOut);
    }
    return success;
}

bool BluetoothCommProtocol::writeRegisters(int mID, int regType, int reg, const QmlRegisterBlock& in) {
    RegisterBlock stdIn = qmlRegisterBlockToStd(in);
    // 调用 ICommProtocol 的 write 方法
    return write(mID, regType, reg, stdIn);
}

void BluetoothCommProtocol::startScan() {
    qDebug() << "蓝牙通信协议：正在开始扫描设备...";
    devices_.clear();
    emit deviceListChanged();
    discoveryAgent_->start();
}

// =========================================================================
// 私有方法和槽函数
// =========================================================================

bool BluetoothCommProtocol::scanAndConnectByNameOrAddress(const QString& nameOrAddress) {
    // 逻辑与之前的版本相同
    QString maybeAddr = nameOrAddress;
    if (maybeAddr.contains(':') || maybeAddr.length() >= 10) {
        // 尝试直接用地址连接
        qDebug() << "蓝牙通信协议：尝试直接使用地址连接 [" << maybeAddr << "]";
        if (connectToAddress(maybeAddr, QBluetoothUuid(QBluetoothUuid::ServiceClassUuid::SerialPort))) {
            connectedName_ = "未知设备"; // 此时无法获取设备名
            return true;
        }
    }

    readBuffer_.clear();
    QEventLoop loop;
    bool foundAndConnected = false;

    // 当连接成功时，退出循环
    connect(this, &BluetoothCommProtocol::connected, &loop, [&](){ foundAndConnected = true; loop.quit(); });
    // 当发现设备时，尝试连接
    connect(discoveryAgent_, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this,
            [this, &nameOrAddress](const QBluetoothDeviceInfo& info) {
                if (info.address().toString() == nameOrAddress || info.name() == nameOrAddress) {
                    discoveryAgent_->stop();
                    qDebug() << "蓝牙通信协议：发现目标设备，正在连接...";
                    if (connectToAddress(info.address().toString(), QBluetoothUuid(QBluetoothUuid::ServiceClassUuid::SerialPort))) {
                        connectedName_ = info.name();
                        connectedAddress_ = info.address().toString();
                    }
                }
            });

    discoveryAgent_->start();
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, &loop, [&](){ loop.quit(); });
    timeoutTimer.start(connectTimeoutMs_);

    loop.exec();

    if (foundAndConnected) {
        return true;
    }

    if (discoveryAgent_->isActive()) discoveryAgent_->stop();
    return false;
}

bool BluetoothCommProtocol::connectToAddress(const QString& address, const QBluetoothUuid& uuid) {
    if (socket_->isOpen()) socket_->close();

    QBluetoothAddress addr(address);
    qDebug() << "蓝牙通信协议：正在连接到地址 [" << address << "]";

    QEventLoop loop;
    bool connected = false;
    connect(socket_, &QBluetoothSocket::connected, &loop, [&](){ connected = true; loop.quit(); });
    // 如果连接失败，也退出循环
    connect(socket_, QOverload<QBluetoothSocket::SocketError>::of(&QBluetoothSocket::errorOccurred), &loop,
            [&](){ loop.quit(); });

    socket_->connectToService(addr, uuid);

    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, [&](){ loop.quit(); });
    timer.start(connectTimeoutMs_);
    loop.exec();

    if (connected && socket_->isOpen()) {
        qDebug() << "蓝牙通信协议：连接成功！";
        connectedAddress_ = address;
        return true;
    }

    qDebug() << "蓝牙通信协议：连接失败，错误信息：" << socket_->errorString();
    if (socket_->isOpen()) socket_->close();
    return false;
}

void BluetoothCommProtocol::onSocketConnected() {
    qDebug() << "蓝牙通信协议：套接字已连接";
    // connected() 信号已在 open() 方法中处理，此槽函数仅用于日志
}

void BluetoothCommProtocol::onSocketDisconnected() {
    qDebug() << "蓝牙通信协议：套接字已断开";
    emit disconnected();
}

void BluetoothCommProtocol::onSocketReadyRead() {
    std::lock_guard<std::mutex> guard(readMutex_);
    QByteArray data = socket_->readAll();
    if (!data.isEmpty()) {
        readBuffer_.append(data);
    }
}

void BluetoothCommProtocol::onDiscoveryDeviceDiscovered(const QBluetoothDeviceInfo& info) {
    QString devName = info.name();
    QString devAddr = info.address().toString();

    qDebug() << "蓝牙通信协议：发现设备: 名称=" << devName << ", 地址=" << devAddr;

    // 创建一个 QVariantMap 来存储设备信息
    QVariantMap deviceMap;
    deviceMap["name"] = devName;
    deviceMap["address"] = devAddr;
    devices_.append(deviceMap);

    emit deviceFound(devName, devAddr);
    emit deviceListChanged();
}

void BluetoothCommProtocol::onDiscoveryFinished() {
    qDebug() << "蓝牙通信协议：设备发现完成";
    emit deviceScanFinished();
}

QString BluetoothCommProtocol::connectedName() const {
    return connectedName_;
}

QVariantList BluetoothCommProtocol::deviceList() const {
    return devices_;
}
