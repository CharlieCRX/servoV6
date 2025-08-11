// F:/project/servoV6/adapters/protocol/BluetoothCommProtocol.h
#ifndef BLUETOOTH_COMM_PROTOCOL_H
#define BLUETOOTH_COMM_PROTOCOL_H

#include "ICommProtocol.h"
#include <QBluetoothSocket>
#include <QBluetoothDeviceInfo>
#include <QBluetoothDeviceDiscoveryAgent> // 新增头文件
#include <memory>
#include <QObject>

class BluetoothCommProtocol : public QObject, public ICommProtocol {
    Q_OBJECT

public:
    explicit BluetoothCommProtocol(QObject* parent = nullptr);
    ~BluetoothCommProtocol() override;

    // ICommProtocol 接口实现
    // 修改 open 方法以支持设备扫描
    bool open(const std::string& deviceName = "", bool reOpen = false) override;
    void close() override;
    bool isOpen() const override;

    bool read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out) override;
    bool write(int mID, int regType, int reg, const RegisterBlock& in) override;

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onErrorOccurred(QBluetoothSocket::SocketError error);

    // 新增设备扫描相关的信号槽
    void deviceDiscovered(const QBluetoothDeviceInfo &device);
    void deviceScanFinished();

private:
    std::shared_ptr<QBluetoothSocket> m_socket;
    std::shared_ptr<QBluetoothDeviceDiscoveryAgent> m_discoveryAgent; // 新增发现代理
    bool m_isOpened = false;
    std::string m_targetDeviceNamePrefix = "MOTOR"; // 目标设备名称前缀
    // 新增：默认目标设备信息
    const std::string m_defaultDeviceName = "MOTOR06ae";
    const std::string m_defaultDeviceMacAddress = "A0:DD:6C:02:06:AE";
};

#endif // BLUETOOTH_COMM_PROTOCOL_H
