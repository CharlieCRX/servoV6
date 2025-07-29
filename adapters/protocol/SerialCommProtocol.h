#ifndef SERIAL_COMM_PROTOCOL_H
#define SERIAL_COMM_PROTOCOL_H

#include <QtSerialBus/qmodbusrtuserialclient.h>
#include <QtSerialBus/QModbusReply>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QCoreApplication>
#include <QTimer>
#include "ICommProtocol.h" // 包含 ICommProtocol 接口
#include <string>
#include <vector>
#include <cstdint> // For uint8_t, uint16_t

// 定义寄存器类型常量，供内部使用
namespace RegisterType {
const int COIL = 0;
const int DISCRETE_INPUT = 1;
const int HOLDING_REGISTER = 3;
const int INPUT_REGISTER = 4;
// ... 可以根据需要添加更多
}

// 串口通信协议的具体实现
class SerialCommProtocol : public ICommProtocol {
public:
    SerialCommProtocol();
    SerialCommProtocol(int baudRate, int dataBits, int stopBits, int flowControl, int parity);
    ~SerialCommProtocol() override;

    // ICommProtocol 接口实现
    bool open(const std::string& deviceName, bool reOpen = false) override;
    void close() override;
    bool isOpen() const override;

    bool read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out) override;
    bool write(int mID, int regType, int reg, const RegisterBlock& in) override;

    bool readReq(int mID, int regType, int startReg, int stopReg) override;
    bool writeReq(int mID, int regType, int reg, const RegisterBlock& in) override;

private:
    bool connected_ = false;
    std::string currentDeviceName_;
};

#endif // SERIAL_COMM_PROTOCOL_H
