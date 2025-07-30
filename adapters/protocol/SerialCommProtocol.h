#ifndef SERIAL_COMM_PROTOCOL_H
#define SERIAL_COMM_PROTOCOL_H

#include <QtSerialBus/qmodbusrtuserialclient.h>
#include <QtSerialBus/QModbusReply>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QCoreApplication>
#include <QTimer>
#include "ICommProtocol.h"
#include <string>

namespace RegisterType {
const int COIL = 0;
const int DISCRETE_INPUT = 1;
const int HOLDING_REGISTER = 3;
const int INPUT_REGISTER = 4;
}

class SerialCommProtocol : public ICommProtocol {
public:
    SerialCommProtocol();
    SerialCommProtocol(int baudRate, int dataBits, int stopBits, int flowControl, int parity);
    ~SerialCommProtocol() override;

    bool open(const std::string& deviceName, bool reOpen = false) override;
    void close() override;
    bool isOpen() const override;

    bool read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out) override;
    bool write(int mID, int regType, int reg, const RegisterBlock& in) override;
public:
    // 读取一个 16-bit 无符号寄存器
    bool readUInt16(int mID, int regType, int startReg, quint16& outVal);

    // 读取两个连续寄存器组成的 32-bit 无符号整数
    bool readUInt32(int mID, int regType, int startReg, quint32& outVal);

    // 读取四个连续寄存器组成的 64-bit 无符号整数
    bool readUInt64(int mID, int regType, int startReg, quint64& outVal);



private:
    bool connected_ = false;
    std::string currentDeviceName_;

    QModbusClient* modbusClient_ = nullptr;
};

#endif // SERIAL_COMM_PROTOCOL_H
