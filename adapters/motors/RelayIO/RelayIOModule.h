#pragma once

#include <cstdint>

class MotorRegisterAccessor;

class RelayIOModule {
public:
    RelayIOModule(MotorRegisterAccessor* accessor, int32_t slaveID, uint32_t baseAddr);

    bool openChannel(int ch);   // 闭合继电器
    bool closeChannel(int ch);  // 断开继电器
    bool readChannelState(int ch, uint16_t& outState); // 读取通道状态

private:
    MotorRegisterAccessor* accessor_;
    int32_t slaveID_;
    uint32_t baseAddr_; // 起始地址，每个通道 +1
};
