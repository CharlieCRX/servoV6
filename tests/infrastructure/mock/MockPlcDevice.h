#pragma once

#include "gmock/gmock.h"
#include "infrastructure/plc/protocol/PlcDevice.h"

/**
 * @brief MockPlcDevice — 用于命令分派单元测试的 GMock 桩
 *
 * 继承 PlcDevice 并 mock writeBool / writeFloat 两个虚拟方法，
 * 使用 StrictMock 确保所有写操作调用都被预期，不存在遗漏的写操作。
 *
 * 构造函数使用 INOVANCE_PROFILE 作为默认 Profile，测试通过。
 */
class MockPlcDevice : public plc::protocol::PlcDevice {
public:
    MockPlcDevice()
        : PlcDevice(plc::protocol::INOVANCE_PROFILE)
    {}

    MOCK_METHOD(CommunicationResult, writeBool,
                (const plc::protocol::RegisterInfo& reg, bool value), (override));
    MOCK_METHOD(CommunicationResult, writeFloat,
                (const plc::protocol::RegisterInfo& reg, float value), (override));
};
