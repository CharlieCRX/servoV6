// F:/project/servoV6/domain/ICommProtocol.h
#ifndef I_COMM_PROTOCOL_H
#define I_COMM_PROTOCOL_H

#include <vector>
#include <cstdint> // For uint8_t, uint16_t

// 定义通用通信协议接口
// 它可以是同步或异步的，这里以同步为例
class ICommProtocol {
public:
    virtual ~ICommProtocol() = default;

    // 示例：读取寄存器（地址，数量）
    virtual std::vector<uint16_t> readRegisters(uint8_t slaveId, uint16_t address, uint16_t quantity) = 0;

    // 示例：写入寄存器（地址，数据）
    virtual bool writeRegisters(uint8_t slaveId, uint16_t address, const std::vector<uint16_t>& data) = 0;

    // 示例：发送自定义命令（原始字节数据）
    virtual bool sendRawCommand(uint8_t slaveId, const std::vector<uint8_t>& command) = 0;

    // 示例：连接/断开连接（如果协议需要显式连接）
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
};

#endif // I_COMM_PROTOCOL_H
