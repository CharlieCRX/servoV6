#ifndef SERIAL_COMM_PROTOCOL_H
#define SERIAL_COMM_PROTOCOL_H

#include "ICommProtocol.h"

#include <QtSerialBus/qmodbusrtuserialclient.h>
#include <QtSerialBus/QModbusReply>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QCoreApplication>
#include <QTimer>
#include <string>

class SerialCommProtocol : public ICommProtocol{
public:
    SerialCommProtocol(QObject* parent = nullptr);
    SerialCommProtocol(int baudRate, int dataBits, int stopBits, int flowControl, int parity, QObject* parent = nullptr);
    ~SerialCommProtocol() override;

    bool open(const std::string& deviceName, bool reOpen = false) override;
    void close() override;
    bool isOpen() const override;

    bool read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out) override;
    bool write(int mID, int regType, int reg, const RegisterBlock& in) override;


private:
    bool connected_ = false;
    std::string currentDeviceName_;
    QModbusClient* modbusClient_ = nullptr;
};

#endif // SERIAL_COMM_PROTOCOL_H
