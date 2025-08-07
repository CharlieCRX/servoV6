#include "RelayIOModule.h"
#include "MotorRegisterAccessor.h"
#include "RegisterType.h"
#include "Logger.h"

RelayIOModule::RelayIOModule(MotorRegisterAccessor* accessor, int32_t slaveID, uint32_t baseAddr)
    : accessor_(accessor), slaveID_(slaveID), baseAddr_(baseAddr) {}

bool RelayIOModule::openChannel(int ch) {
    uint32_t addr = baseAddr_ + ch;
    bool ok = accessor_->writeReg(slaveID_, RegisterType::HOLDING_REGISTER, addr, 0x0001, 1);
    if (ok) {
        LOG_INFO("闭合继电器通道 {}", ch + 1);
    }
    return ok;
}

bool RelayIOModule::closeChannel(int ch) {
    uint32_t addr = baseAddr_ + ch;
    bool ok = accessor_->writeReg(slaveID_, RegisterType::HOLDING_REGISTER, addr, 0x0000, 1);
    if (ok) {
        LOG_INFO("断开继电器通道 {}", ch + 1);
    }
    return ok;
}

bool RelayIOModule::readChannelState(int ch, uint16_t& outState) {
    uint64_t value = 0;
    uint32_t addr = baseAddr_ + ch;
    if (!accessor_->readReg(slaveID_, RegisterType::HOLDING_REGISTER, addr, value, 1)) {
        return false;
    }
    outState = static_cast<uint16_t>(value);
    LOG_INFO("读取继电器通道 {} 状态: {}", ch + 1, outState);
    return true;
}
