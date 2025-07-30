#include "SerialCommProtocol.h"
#include "Logger.h"

#include <QModbusDataUnit>
#include <QModbusDevice>
#include <QModbusReply>
#include <QVariant>
#include <algorithm> // for std::min

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

// SerialCommProtocol.cpp 内部私有函数
static quint64 combineRegistersTo64(const std::vector<uint16_t>& data) {
    quint64 val = 0;
    int count = std::min(static_cast<int>(data.size()), 4);
    for (int i = 0; i < count; ++i) {
        val |= static_cast<quint64>(data[i]) << (16 * i);
    }
    return val;
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
    // 1. 前置条件检查：串口是否打开，Modbus客户端是否有效
    if (!modbusClient_ || modbusClient_->state() != QModbusDevice::ConnectedState) {
        LOG_INFO("读取失败：串口未打开或Modbus客户端无效。");
        return false;
    }

    // 2. 寄存器范围校验
    if (startReg > stopReg) {
        LOG_INFO("读取失败：寄存器范围非法 (startReg > stopReg)。");
        return false;
    }

    // 3. 寄存器类型校验
    QModbusDataUnit::RegisterType qtRegType;
    switch (regType) {
    case RegisterType::COIL: qtRegType = QModbusDataUnit::Coils; break;
    case RegisterType::DISCRETE_INPUT: qtRegType = QModbusDataUnit::DiscreteInputs; break;
    case RegisterType::HOLDING_REGISTER: qtRegType = QModbusDataUnit::HoldingRegisters; break;
    case RegisterType::INPUT_REGISTER: qtRegType = QModbusDataUnit::InputRegisters; break;
    default:
        LOG_INFO("读取失败：不支持的寄存器类型 {}。", regType);
        return false;
    }

    // 4. 构建 Modbus 数据单元并发送读取请求
    int count = stopReg - startReg + 1;
    QModbusDataUnit readUnit(qtRegType, startReg, count);
    QModbusReply *reply = modbusClient_->sendReadRequest(readUnit, mID);

    // 5. 检查请求是否成功发送
    if (!reply) {
        LOG_INFO("读取失败：发送读请求失败，Modbus客户端状态：{}。", modbusClient_->errorString().toStdString());
        return false;
    }

    // 6. 同步等待 Modbus 响应或超时
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true); // 定时器只触发一次

    // 连接信号和槽：当reply完成或定时器超时时，退出事件循环
    QObject::connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(3000); // 设置超时时间为 3 秒
    loop.exec();       // 启动局部事件循环，阻塞当前线程直到信号发出

    // 7. **重要：在处理完 reply 后，断开信号槽连接，防止 dangling pointer 问题**
    QObject::disconnect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
    QObject::disconnect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    // 8. 检查是否超时
    if (!timer.isActive()) { // 如果定时器未激活，说明是超时退出
        LOG_INFO("读取失败：请求超时。");
        delete reply; // **关键修复：直接 delete reply**
        return false;
    }

    // 9. 检查 Modbus 错误
    if (reply->error() != QModbusDevice::NoError) {
        LOG_INFO("读取失败：Modbus错误码 {} - {}", static_cast<int>(reply->error()), reply->errorString().toStdString());
        delete reply; // **关键修复：直接 delete reply**
        return false;
    }

    // 10. 处理成功响应数据
    const QModbusDataUnit unit = reply->result();
    out.data.clear();
    for (uint i = 0; i < unit.valueCount(); ++i) {
        out.data.push_back(unit.value(i));
    }

    // 11. **关键修复：在函数结束前直接删除 reply 对象**
    delete reply;

    // 12. 构建日志信息并记录
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

// ---

bool SerialCommProtocol::write(int mID, int regType, int reg, const RegisterBlock& in)
{
    // 1. 前置条件检查：串口是否打开，Modbus客户端是否有效
    if (!connected_ || !modbusClient_ || modbusClient_->state() != QModbusDevice::ConnectedState) {
        LOG_ERROR("写入失败：串口未连接或Modbus客户端无效。");
        return false;
    }

    // 2. 寄存器类型校验（目前仅支持 HOLDING_REGISTER）
    if (regType != RegisterType::HOLDING_REGISTER) {
        LOG_ERROR("写入失败：不支持的寄存器类型 {}。", regType);
        return false;
    }

    // 3. 输入数据校验
    if (in.data.empty()) {
        LOG_ERROR("写入失败：输入数据为空。");
        return false;
    }

    // 4. 构建 Modbus 数据单元并发送写入请求
    // 注意：这里的实现（writeUnit.setValue(0, in.data[0]);）看起来只支持写入一个寄存器。
    // 如果需要写入多个寄存器，需要根据 in.data 的大小调整 count 和循环设置 value。
    // QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, reg, in.data.size()); // 如果要支持写入多个
    // for (int i = 0; i < in.data.size(); ++i) { writeUnit.setValue(i, in.data[i]); }
    QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, reg, 1);
    writeUnit.setValue(0, in.data[0]);

    QModbusReply* reply = modbusClient_->sendWriteRequest(writeUnit, mID);

    // 5. 检查请求是否成功发送
    if (!reply) {
        LOG_ERROR("写入失败：无法发送 Modbus 请求，Modbus客户端状态：{}。", modbusClient_->errorString().toStdString());
        return false;
    }

    // 6. 同步等待 Modbus 响应或超时
    QEventLoop loop;
    QObject::connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);

    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(1000); // 最长等待 1 秒

    loop.exec(); // 启动局部事件循环

    // 7. **重要：在处理完 reply 后，断开信号槽连接，防止 dangling pointer 问题**
    QObject::disconnect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
    QObject::disconnect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    // 8. 检查是否超时
    if (!timeoutTimer.isActive()) {
        LOG_ERROR("写入失败：等待响应超时。");
        delete reply; // **关键修复：直接 delete reply**
        return false;
    }

    timeoutTimer.stop(); // 停止定时器

    // 9. 检查 Modbus 错误
    if (reply->error() != QModbusDevice::NoError) {
        LOG_ERROR("写入失败：Modbus错误码 {} - {}", static_cast<int>(reply->error()), reply->errorString().toStdString());
        delete reply; // **关键修复：直接 delete reply**
        return false;
    }

    // 10. **关键修复：在函数结束前直接删除 reply 对象**
    delete reply;

    // 11. 记录成功信息
    LOG_INFO("写入成功：从站ID={}, 寄存器类型={}, 地址={}, 值={}。", mID, regType, reg, in.data[0]);
    return true;
}

bool SerialCommProtocol::readUInt16(int mID, int regType, int reg, uint16_t& outVal) {
    RegisterBlock regData;
    if (!read(mID, regType, reg, reg, regData)) {
        return false;
    }
    if (regData.data.size() < 1) return false;

    outVal = regData.data[0];
    LOG_INFO("读取 uint16 寄存器[0x{:04X}]: {} (0x{:04X})", reg, outVal, outVal);
    return true;
}

bool SerialCommProtocol::readUInt32(int mID, int regType, int reg, uint32_t& outVal) {
    RegisterBlock regData;
    if (!read(mID, regType, reg, reg + 1, regData)) {
        return false;
    }
    if (regData.data.size() < 2) return false;

    quint64 val = combineRegistersTo64(regData.data);
    outVal = static_cast<uint32_t>(val);
    LOG_INFO("读取 uint32 寄存器[0x{:04X}~0x{:04X}]: {} (0x{:08X})", reg, reg + 1, outVal, outVal);
    return true;
}

bool SerialCommProtocol::readUInt64(int mID, int regType, int reg, uint64_t& outVal) {
    RegisterBlock regData;
    if (!read(mID, regType, reg, reg + 3, regData)) {
        return false;
    }
    if (regData.data.size() < 4) return false;

    outVal = combineRegistersTo64(regData.data);
    LOG_INFO("读取 uint64 寄存器[0x{:04X}~0x{:04X}]: {} (0x{:016X})", reg, reg + 3, outVal, outVal);
    return true;
}


bool SerialCommProtocol::writeUInt16(int mID, int regType, int reg, uint16_t value) {
    RegisterBlock regData;
    regData.data.push_back(value);
    bool ret = write(mID, regType, reg, regData);
    if (ret) {
        LOG_INFO("写入 uint16 寄存器[0x{:04X}]: {} (0x{:04X})", reg, value, value);
    } else {
        LOG_ERROR("写入 uint16 寄存器失败[0x{:04X}]", reg);
    }
    return ret;
}

bool SerialCommProtocol::writeUInt32(int mID, int regType, int reg, uint32_t value) {
    RegisterBlock regData;
    regData.data.resize(2);
    regData.data[0] = static_cast<uint16_t>(value & 0xFFFF);
    regData.data[1] = static_cast<uint16_t>((value >> 16) & 0xFFFF);
    bool ret = write(mID, regType, reg, regData);
    if (ret) {
        LOG_INFO("写入 uint32 寄存器[0x{:04X}~0x{:04X}]: {} (0x{:08X})", reg, reg + 1, value, value);
    } else {
        LOG_ERROR("写入 uint32 寄存器失败[0x{:04X}~0x{:04X}]", reg, reg + 1);
    }
    return ret;
}

bool SerialCommProtocol::writeUInt64(int mID, int regType, int reg, uint64_t value) {
    RegisterBlock regData;
    regData.data.resize(4);
    regData.data[0] = static_cast<uint16_t>(value & 0xFFFF);
    regData.data[1] = static_cast<uint16_t>((value >> 16) & 0xFFFF);
    regData.data[2] = static_cast<uint16_t>((value >> 32) & 0xFFFF);
    regData.data[3] = static_cast<uint16_t>((value >> 48) & 0xFFFF);
    bool ret = write(mID, regType, reg, regData);
    if (ret) {
        LOG_INFO("写入 uint64 寄存器[0x{:04X}~0x{:04X}]: {} (0x{:016X})", reg, reg + 3, value, value);
    } else {
        LOG_ERROR("写入 uint64 寄存器失败[0x{:04X}~0x{:04X}]", reg, reg + 3);
    }
    return ret;
}
