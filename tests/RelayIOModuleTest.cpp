#include "RelayIOModuleTest.h"
#include "SerialCommProtocol.h"
#include "MotorRegisterAccessor.h"
#include "RegisterType.h"
#include "RelayIO/RelayIOModule.h"
#include <QTest>

static const std::string VALID_PORT_NAME = "COM17";  // 请替换为你真实串口
static const int SLAVE_ID = 1;
static const uint32_t HOLDING_BASE_ADDR = 0; // 继电器线圈起始地址

void RelayIOModuleTest::defaultConstructor_shouldSetDefaults()
{
    SerialCommProtocol protocol;
    QVERIFY(!protocol.isOpen());
}

void RelayIOModuleTest::customConstructor_shouldSetParametersCorrectly()
{
    SerialCommProtocol protocol(38400, 8, 1, QSerialPort::NoFlowControl, QSerialPort::NoParity);
    QVERIFY(!protocol.isOpen());
}

void RelayIOModuleTest::open_withInvalidPort_shouldFail()
{
    SerialCommProtocol protocol;
    QVERIFY(!protocol.open("INVALID_PORT"));
    QVERIFY(!protocol.isOpen());
}

void RelayIOModuleTest::open_withValidPort_shouldSucceed()
{
    SerialCommProtocol protocol;
    QVERIFY(protocol.open(VALID_PORT_NAME));
    QVERIFY(protocol.isOpen());
    protocol.close();
}

void RelayIOModuleTest::relayChannels_openClose_shouldSucceed()
{
    SerialCommProtocol protocol(38400, 8, 1, QSerialPort::NoFlowControl, QSerialPort::NoParity);
    QVERIFY(protocol.open(VALID_PORT_NAME));

    MotorRegisterAccessor accessor(&protocol);

    for (int ch = 0; ch < 8; ++ch) {
        uint32_t addr = HOLDING_BASE_ADDR + ch;

        // 闭合继电器
        QVERIFY2(accessor.writeReg(SLAVE_ID, RegisterType::HOLDING_REGISTER, addr, 0x0001, 1),
                 QString("闭合继电器 %1 失败").arg(ch + 1).toUtf8());
        QTest::qSleep(50);

        // 断开继电器
        QVERIFY2(accessor.writeReg(SLAVE_ID, RegisterType::HOLDING_REGISTER, addr, 0x0000, 1),
                 QString("断开继电器 %1 失败").arg(ch + 1).toUtf8());
        QTest::qSleep(50);
    }

    protocol.close();
}

void RelayIOModuleTest::readRelayState_shouldReturnExpectedValue()
{
    SerialCommProtocol protocol(38400, 8, 1, QSerialPort::NoFlowControl, QSerialPort::NoParity);
    QVERIFY(protocol.open(VALID_PORT_NAME));

    MotorRegisterAccessor accessor(&protocol);
    const uint32_t addr = HOLDING_BASE_ADDR + 0;

    // 写入闭合状态
    QVERIFY(accessor.writeReg(SLAVE_ID, RegisterType::HOLDING_REGISTER, addr, 0x0001, 1));
    QTest::qSleep(100);

    uint64_t state = 0;
    QVERIFY(accessor.readReg(SLAVE_ID, RegisterType::HOLDING_REGISTER, addr, state, 1));
    QVERIFY(state == 0x0001);

    // 写入断开状态
    QVERIFY(accessor.writeReg(SLAVE_ID, RegisterType::HOLDING_REGISTER, addr, 0x0000, 1));
    QTest::qSleep(100);

    QVERIFY(accessor.readReg(SLAVE_ID, RegisterType::HOLDING_REGISTER, addr, state, 1));
    QVERIFY(state == 0x0000);

    protocol.close();
}


void RelayIOModuleTest::relayIOModule_openCloseChannel_shouldSucceed()
{
    SerialCommProtocol protocol(38400, 8, 1, QSerialPort::NoFlowControl, QSerialPort::NoParity);
    QVERIFY(protocol.open(VALID_PORT_NAME));

    MotorRegisterAccessor accessor(&protocol);
    RelayIOModule relayModule(&accessor, SLAVE_ID, HOLDING_BASE_ADDR);

    for (int ch = 0; ch < 8; ++ch) {
        QVERIFY2(relayModule.openChannel(ch), QString("闭合继电器 %1 失败").arg(ch + 1).toUtf8());
        QTest::qSleep(50);

        QVERIFY2(relayModule.closeChannel(ch), QString("断开继电器 %1 失败").arg(ch + 1).toUtf8());
        QTest::qSleep(50);
    }
}

void RelayIOModuleTest::relayIOModule_readChannelState_shouldReturnExpectedValue()
{
    SerialCommProtocol protocol(38400, 8, 1, QSerialPort::NoFlowControl, QSerialPort::NoParity);
    QVERIFY(protocol.open(VALID_PORT_NAME));

    MotorRegisterAccessor accessor(&protocol);
    RelayIOModule relayModule(&accessor, SLAVE_ID, HOLDING_BASE_ADDR);

    uint16_t state = 0;
    for (int ch = 0; ch < 8; ++ch) {
        QVERIFY(relayModule.openChannel(ch));
        QTest::qSleep(100);

        QVERIFY(relayModule.readChannelState(ch, state));
        QVERIFY(state == 0x0001);

        QVERIFY(relayModule.closeChannel(ch));
        QTest::qSleep(100);

        QVERIFY(relayModule.readChannelState(ch, state));
        QVERIFY(state == 0x0000);
    }
}

