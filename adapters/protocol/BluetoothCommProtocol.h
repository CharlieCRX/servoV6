// BluetoothCommProtocol.h

#ifndef BLUETOOTH_COMM_PROTOCOL_H
#define BLUETOOTH_COMM_PROTOCOL_H

#include <QObject>
#include <QBluetoothSocket>
#include <QBluetoothDeviceInfo>
#include <QBluetoothDeviceDiscoveryAgent>
#include <memory>
#include "ICommProtocol.h"
#include <QEventLoop> // 新增头文件

// 你可能需要根据你的项目定义这些枚举或类型
enum RegisterType {
    RelayStatus = 3
};

class BluetoothCommProtocol : public QObject, public ICommProtocol {
    Q_OBJECT

public:
    explicit BluetoothCommProtocol(QObject* parent = nullptr);
    ~BluetoothCommProtocol() override;

    // ICommProtocol 接口实现
    bool open(const std::string& deviceName = "", bool reOpen = false) override;
    void close() override;
    bool isOpen() const override;

    bool read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out) override;
    bool write(int mID, int regType, int reg, const RegisterBlock& in) override;

signals:
    // QObject 提供的信号，用于通知连接状态和错误
    void connected();
    void disconnected();
    void error(const QString& errorString);
    void readyRead(const QByteArray& data);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onErrorOccurred(QBluetoothSocket::SocketError socketError);

    // 设备扫描相关的信号槽
    void deviceDiscovered(const QBluetoothDeviceInfo &device);
    void deviceScanFinished();

private:
    // Modbus RTU 协议相关的辅助函数
    quint16 calculateCRC(const QByteArray& data);
    bool parseModbusResponse(int expectedRegCount, const QByteArray& data, RegisterBlock& out);

    std::shared_ptr<QBluetoothSocket> m_socket;
    std::shared_ptr<QBluetoothDeviceDiscoveryAgent> m_discoveryAgent;

    bool m_isOpened = false;
    std::string m_targetDeviceNamePrefix = "MOTOR";
    QEventLoop m_discoveryLoop; // 用于阻塞等待设备扫描
    std::vector<std::string> m_scannedDeviceNames;  // 扫描到的设备名集合

};

#endif // BLUETOOTH_COMM_PROTOCOL_H
