// F:/project/servoV6/adapters/protocol/SerialComm/SerialCommProtocol.cpp

#include "SerialCommProtocol.h"
#include "Logger.h" // 正式引入 Logger

SerialCommProtocol::SerialCommProtocol() : connected_(false) {
    // 构造函数中初始化连接状态为 false
    LOG_INFO("SerialCommProtocol: Instance created.");
}

SerialCommProtocol::~SerialCommProtocol() {
    // 析构函数中确保关闭连接
    close();
    LOG_INFO("SerialCommProtocol: Instance destroyed.");
}

bool SerialCommProtocol::open(const std::string& deviceName, bool reOpen) {
    // 如果已经连接，且不是强制重开，则直接返回 true (模拟成功)
    if (connected_ && !reOpen) {
        LOG_WARN("SerialCommProtocol: Already connected to {}. Use reOpen=true to force reopen.", currentDeviceName_);
        return true;
    }

    // 如果已经连接且是强制重开，则先关闭现有连接
    if (connected_ && reOpen) {
        close();
    }

    LOG_INFO("SerialCommProtocol: Attempting to open serial port: {}{}.", deviceName, reOpen ? " (reopen)" : "");

    // *** 实际的串口打开逻辑会在这里实现 ***
    // 例如：调用 Boost.Asio 或平台特定的 API (CreateFile, open)
    // 这里我们只是模拟成功
    connected_ = true;
    currentDeviceName_ = deviceName; // 保存设备名
    LOG_INFO("SerialCommProtocol: Serial port {} opened successfully (simulated).", deviceName);
    return true; // 模拟成功
}

void SerialCommProtocol::close() {
    // 如果当前是连接状态，则关闭
    if (connected_) {
        LOG_INFO("SerialCommProtocol: Closing serial port: {}.", currentDeviceName_);
        // *** 实际的串口关闭逻辑会在这里实现 ***
        // 例如：调用 CloseHandle, close
        connected_ = false;
        currentDeviceName_ = ""; // 清空设备名
        LOG_INFO("SerialCommProtocol: Serial port closed (simulated).");
    }
}

bool SerialCommProtocol::isOpen() const {
    // 返回当前的连接状态
    return connected_;
}

bool SerialCommProtocol::read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out) {
    // 未连接时，操作失败
    if (!connected_) {
        LOG_ERROR("SerialCommProtocol: Not connected to perform read operation.");
        return false;
    }

    int quantity = stopReg - startReg + 1;
    // 请求数量无效时，操作失败
    if (quantity <= 0) {
        LOG_ERROR("SerialCommProtocol: Invalid quantity for read: {}.", quantity);
        return false;
    }

    LOG_INFO("SerialCommProtocol: Read operation (mID={}, regType={}, startReg={}, stopReg={}).", mID, regType, startReg, stopReg);

    out.data.clear();     // 清空旧数据
    out.data.resize(quantity); // 预先分配空间

    // 根据 regType 模拟不同类型寄存器的读取行为
    switch (regType) {
    case RegisterType::COIL:
        LOG_DEBUG("SerialCommProtocol: Reading Coils (simulated).");
        // 模拟线圈状态（1位），用 uint16_t 表示
        for (int i = 0; i < quantity; ++i) {
            out.data[i] = ((startReg + i) % 2 == 0) ? 0x0001 : 0x0000; // 模拟偶数地址为1，奇数地址为0
        }
        break;
    case RegisterType::DISCRETE_INPUT:
        LOG_DEBUG("SerialCommProtocol: Reading Discrete Inputs (simulated).");
        // 模拟离散输入状态
        for (int i = 0; i < quantity; ++i) {
            out.data[i] = ((startReg + i) % 3 == 0) ? 0x0001 : 0x0000; // 模拟每3个地址循环
        }
        break;
    case RegisterType::HOLDING_REGISTER:
        LOG_DEBUG("SerialCommProtocol: Reading Holding Registers (simulated).");
        // 模拟保持寄存器数据
        for (int i = 0; i < quantity; ++i) {
            out.data[i] = static_cast<uint16_t>(0x1000 + startReg + i); // 模拟数据递增
        }
        break;
    case RegisterType::INPUT_REGISTER:
        LOG_DEBUG("SerialCommProtocol: Reading Input Registers (simulated).");
        // 模拟输入寄存器数据
        for (int i = 0; i < quantity; ++i) {
            out.data[i] = static_cast<uint16_t>(0x2000 + startReg + i); // 模拟数据递增
        }
        break;
    default:
        LOG_ERROR("SerialCommProtocol: Unknown or unsupported register type for read: {}.", regType);
        out.data.clear(); // 未知类型，清空数据
        return false;
    }
    return true; // 模拟读取成功
}

bool SerialCommProtocol::write(int mID, int regType, int reg, const RegisterBlock& in) {
    // 未连接时，操作失败
    if (!connected_) {
        LOG_ERROR("SerialCommProtocol: Not connected to perform write operation.");
        return false;
    }

    // 写入数据为空时，操作失败
    if (in.data.empty()) {
        LOG_WARN("SerialCommProtocol: Write operation called with empty data block. No action taken.");
        return false;
    }

    LOG_INFO("SerialCommProtocol: Write operation (mID={}, regType={}, reg={}, data size={}).", mID, regType, reg, in.data.size());

    // 根据 regType 模拟不同类型寄存器的写入行为
    switch (regType) {
    case RegisterType::COIL:
        LOG_DEBUG("SerialCommProtocol: Writing Coil(s) (simulated). Address: {}, Value(s): {}", reg, in.data.size() == 1 ? std::to_string(in.data[0]) : "multiple");
        // 实际这里会发送写入线圈的命令
        break;
    case RegisterType::HOLDING_REGISTER:
        LOG_DEBUG("SerialCommProtocol: Writing Holding Register(s) (simulated). Address: {}, Value(s): {}", reg, in.data.size() == 1 ? std::to_string(in.data[0]) : "multiple");
        // 实际这里会发送写入保持寄存器的命令
        break;
    default:
        LOG_ERROR("SerialCommProtocol: Unknown or unsupported register type for write: {}.", regType);
        return false;
    }
    return true; // 模拟写入成功
}

bool SerialCommProtocol::readReq(int mID, int regType, int startReg, int stopReg) {
    // 未连接时，请求失败
    if (!connected_) {
        LOG_ERROR("SerialCommProtocol: Not connected to send read request.");
        return false;
    }
    LOG_INFO("SerialCommProtocol: Sending read request (mID={}, regType={}, startReg={}, stopReg={}).", mID, regType, startReg, stopReg);
    // 在异步系统中，这里会将请求放入队列。这里只是模拟请求已发送。
    return true; // 模拟请求发送成功
}

bool SerialCommProtocol::writeReq(int mID, int regType, int reg, const RegisterBlock& in) {
    // 未连接时，请求失败
    if (!connected_) {
        LOG_ERROR("SerialCommProtocol: Not connected to send write request.");
        return false;
    }
    LOG_INFO("SerialCommProtocol: Sending write request (mID={}, regType={}, reg={}, data size={}).", mID, regType, reg, in.data.size());
    // 在异步系统中，这里会将请求放入队列。这里只是模拟请求已发送。
    return true; // 模拟请求发送成功
}
