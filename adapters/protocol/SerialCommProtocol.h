#ifndef SERIAL_COMM_PROTOCOL_H
#define SERIAL_COMM_PROTOCOL_H

#include "ICommProtocol.h"
#include "IRegisterAccessor.h"   // 新增

#include <QtSerialBus/qmodbusrtuserialclient.h>
#include <QtSerialBus/QModbusReply>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QCoreApplication>
#include <QTimer>
#include <string>

namespace RegisterType {
const int COIL = 0;
const int DISCRETE_INPUT = 1;
const int HOLDING_REGISTER = 3;
const int INPUT_REGISTER = 4;
}

class SerialCommProtocol : public ICommProtocol, public IRegisterAccessor {
public:
    SerialCommProtocol();
    SerialCommProtocol(int baudRate, int dataBits, int stopBits, int flowControl, int parity);
    ~SerialCommProtocol() override;

    // ICommProtocol 实现
    bool open(const std::string& deviceName, bool reOpen = false) override;
    void close() override;
    bool isOpen() const override;

    bool read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out) override;
    bool write(int mID, int regType, int reg, const RegisterBlock& in) override;

    // IRegisterAccessor 实现
    bool readUInt16(int mID, int regType, int startReg, uint16_t& outVal) override;
    bool readUInt32(int mID, int regType, int startReg, uint32_t& outVal) override;
    bool readUInt64(int mID, int regType, int startReg, uint64_t& outVal) override;

private:
    bool connected_ = false;
    std::string currentDeviceName_;
    QModbusClient* modbusClient_ = nullptr;
};

#endif // SERIAL_COMM_PROTOCOL_H
