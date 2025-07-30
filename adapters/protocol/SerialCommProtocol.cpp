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

bool SerialCommProtocol::read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out)
{
    if (!modbusClient_ || modbusClient_->state() != QModbusDevice::ConnectedState) {
        LOG_INFO("读取失败：串口未打开");
        return false;
    }

    if (startReg > stopReg) {
        LOG_INFO("读取失败：寄存器范围非法 (startReg > stopReg)");
        return false;
    }

    switch (regType) {
    case RegisterType::COIL:
    case RegisterType::DISCRETE_INPUT:
    case RegisterType::HOLDING_REGISTER:
    case RegisterType::INPUT_REGISTER:
        break;
    default:
        LOG_INFO("读取失败：不支持的寄存器类型");
        return false;
    }

    int count = stopReg - startReg + 1;
    QModbusDataUnit::RegisterType qtRegType;
    switch (regType) {
    case RegisterType::COIL: qtRegType = QModbusDataUnit::Coils; break;
    case RegisterType::DISCRETE_INPUT: qtRegType = QModbusDataUnit::DiscreteInputs; break;
    case RegisterType::HOLDING_REGISTER: qtRegType = QModbusDataUnit::HoldingRegisters; break;
    case RegisterType::INPUT_REGISTER: qtRegType = QModbusDataUnit::InputRegisters; break;
    }

    QModbusDataUnit readUnit(qtRegType, startReg, count);
    QModbusReply *reply = modbusClient_->sendReadRequest(readUnit, mID);
    if (!reply) {
        LOG_INFO("读取失败：发送读请求失败");
        return false;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(3000);
    loop.exec();

    if (!timer.isActive()) {
        LOG_INFO("读取失败：请求超时");
        reply->deleteLater();
        return false;
    }

    if (reply->error() != QModbusDevice::NoError) {
        LOG_INFO("读取失败：Modbus错误 {}", static_cast<int>(reply->error()));
        reply->deleteLater();
        return false;
    }

    const QModbusDataUnit unit = reply->result();
    out.data.clear();
    for (uint i = 0; i < unit.valueCount(); ++i) {
        out.data.push_back(unit.value(i));
    }

    reply->deleteLater();

    // 构建详细的寄存器输出字符串
    std::string values_str;
    for (size_t i = 0; i < out.data.size(); ++i) {
        uint16_t val = out.data[i];
        int addr = startReg + static_cast<int>(i);
        values_str += fmt::format("[{}]={} (0x{:04X}) ", addr, val, val);
    }

    LOG_INFO("读取成功：从从站 {} 读取类型 {}，地址范围 [{}-{}]，共 {} 个寄存器，值：{}",
             mID, regType, startReg, stopReg, out.data.size(), values_str);

    return true;
}

bool SerialCommProtocol::write(int mID, int regType, int reg, const RegisterBlock& in)
{
    if (!connected_ || !modbusClient_ || modbusClient_->state() != QModbusDevice::ConnectedState) {
        LOG_ERROR("写入失败：串口未连接。");
        return false;
    }

    if (regType != RegisterType::HOLDING_REGISTER) {
        LOG_ERROR("写入失败：不支持的寄存器类型 {}", regType);
        return false;
    }

    if (in.data.empty()) {
        LOG_ERROR("写入失败：输入数据为空。");
        return false;
    }

    // 目前仅支持写一个寄存器
    QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, reg, 1);
    writeUnit.setValue(0, in.data[0]);

    QModbusReply* reply = modbusClient_->sendWriteRequest(writeUnit, mID);
    if (!reply) {
        LOG_ERROR("写入失败：无法发送 Modbus 请求。");
        return false;
    }

    // 同步等待结果
    QEventLoop loop;
    QObject::connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);

    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(1000);  // 最长等待 1 秒

    loop.exec();

    if (!timeoutTimer.isActive()) {
        LOG_ERROR("写入失败：等待响应超时。");
        reply->deleteLater();
        return false;
    }

    timeoutTimer.stop();

    if (reply->error() != QModbusDevice::NoError) {
        LOG_ERROR("写入失败：{}", reply->errorString().toStdString());
        reply->deleteLater();
        return false;
    }

    LOG_INFO("写入成功：mID={}, regType={}, reg={}, value={}", mID, regType, reg, in.data[0]);
    reply->deleteLater();
    return true;
}

bool SerialCommProtocol::readReq(int mID, int regType, int startReg, int stopReg) {
    LOG_INFO("尚未实现 readReq 功能。");
    return true;
}

bool SerialCommProtocol::writeReq(int mID, int regType, int reg, const RegisterBlock& in) {
    LOG_INFO("尚未实现 writeReq 功能。");
    return true;
}
