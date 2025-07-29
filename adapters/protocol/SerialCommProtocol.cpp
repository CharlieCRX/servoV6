#include "SerialCommProtocol.h"
#include "Logger.h"

#include <QModbusDataUnit>
#include <QModbusDevice>
#include <QModbusReply>
#include <QVariant>

SerialCommProtocol::SerialCommProtocol()
    : SerialCommProtocol(9600, 8, 2, 0, 0) {}

SerialCommProtocol::SerialCommProtocol(int baudRate, int dataBits, int stopBits, int flowControl, int parity) {
    modbusClient_ = new QModbusRtuSerialClient;

    modbusClient_->setConnectionParameter(QModbusDevice::SerialBaudRateParameter, QVariant::fromValue(baudRate));
    modbusClient_->setConnectionParameter(QModbusDevice::SerialDataBitsParameter, QVariant::fromValue(dataBits));
    modbusClient_->setConnectionParameter(QModbusDevice::SerialStopBitsParameter, QVariant::fromValue(stopBits));
    modbusClient_->setConnectionParameter(QModbusDevice::SerialParityParameter, QVariant::fromValue(parity));
    modbusClient_->setConnectionParameter(QModbusDevice::SerialPortNameParameter, "");  // 初始化为空串口名

    LOG_INFO("串口参数设置完成：波特率={}, 数据位={}, 停止位={}, 校验={}, 流控={}",
             baudRate, dataBits, stopBits, parity, flowControl);
}


SerialCommProtocol::~SerialCommProtocol() {
    close();
    delete modbusClient_;
}

bool SerialCommProtocol::open(const std::string& deviceName, bool reOpen) {
    if (connected_ && !reOpen) return true;

    if (modbusClient_->state() == QModbusDevice::ConnectedState) {
        modbusClient_->disconnectDevice();
    }

    modbusClient_->setConnectionParameter(QModbusDevice::SerialPortNameParameter, QString::fromStdString(deviceName));
    if (!modbusClient_->connectDevice()) {
        LOG_ERROR("连接串口失败：{}", modbusClient_->errorString().toStdString());
        return false;
    }

    connected_ = true;
    currentDeviceName_ = deviceName;
    LOG_INFO("串口连接成功：{}", deviceName);
    return true;
}

void SerialCommProtocol::close() {
    if (connected_) {
        modbusClient_->disconnectDevice();
        connected_ = false;
        LOG_INFO("串口连接已关闭。");
    }
}

bool SerialCommProtocol::isOpen() const {
    return connected_;
}

bool SerialCommProtocol::read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out) {
    LOG_INFO("尚未实现 read 功能。");
    return false;
}

bool SerialCommProtocol::write(int mID, int regType, int reg, const RegisterBlock& in) {
    LOG_INFO("尚未实现 write 功能。");
    return false;
}

bool SerialCommProtocol::readReq(int mID, int regType, int startReg, int stopReg) {
    LOG_INFO("尚未实现 readReq 功能。");
    return false;
}

bool SerialCommProtocol::writeReq(int mID, int regType, int reg, const RegisterBlock& in) {
    LOG_INFO("尚未实现 writeReq 功能。");
    return false;
}
