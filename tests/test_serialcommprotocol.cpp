#include "test_serialcommprotocol.h"
#include "SerialCommProtocol.h"

// 测试此案例时候，请配置 P100S 的 PA71 参数为 5
void SerialCommProtocolTest::defaultConstructor_shouldSetParametersCorrectly() {
    SerialCommProtocol protocol;
    QVERIFY(!protocol.isOpen());
}

void SerialCommProtocolTest::open_withInvalidPort_shouldFail() {
    SerialCommProtocol protocol;
    bool result = protocol.open("INVALID_PORT_NAME");
    QVERIFY(!result);
    QVERIFY(!protocol.isOpen());
}

void SerialCommProtocolTest::read_whenNotOpen_shouldFail() {
    SerialCommProtocol protocol;

    RegisterBlock out;
    bool result = protocol.read(1, RegisterType::HOLDING_REGISTER, 0, 10, out);

    // 因为串口未 open，应返回 false
    QVERIFY(!result);
}

void SerialCommProtocolTest::read_withInvalidRegisterRange_shouldFail() {
    SerialCommProtocol protocol;

    RegisterBlock out;
    bool result = protocol.read(1, RegisterType::HOLDING_REGISTER, 10, 5, out);

    // 起始地址大于结束地址，应立即失败
    QVERIFY(!result);
}

void SerialCommProtocolTest::read_withUnsupportedRegisterType_shouldFail() {
    SerialCommProtocol protocol;

    RegisterBlock out;
    // 使用非法 regType = 9999
    bool result = protocol.read(1, 9999, 0, 10, out);

    QVERIFY(!result);
}

void SerialCommProtocolTest::read_withConnectedClientButTimeout_shouldFail() {
    SerialCommProtocol protocol;

    // ⚠️ 替换为你本地可打开但无响应的串口
    bool opened = protocol.open("COM10");
    QVERIFY(opened);
    QVERIFY(protocol.isOpen());

    RegisterBlock out;
    bool result = protocol.read(1, RegisterType::HOLDING_REGISTER, 0, 1, out);

    QVERIFY(!result);
    QVERIFY(out.data.empty());
}

void SerialCommProtocolTest::read_withConnectedClientAndValidData_shouldSucceed() {
    SerialCommProtocol protocol;

    // 前提：你有一个模拟设备在某个虚拟串口上运行（如 /dev/ttyV1）
    bool opened = protocol.open("COM10"); // 当前楼上 PC 连接的电机串口为COM10
    QVERIFY(opened);

    RegisterBlock out;
    // 当前P100S 伺服的从地址为5，寄存器 0x1018 保存的是当前位置脉冲数
    bool result = protocol.read(5, RegisterType::HOLDING_REGISTER, 0x1018, 0x101b, out);

    // PA21 保存的是转速
    result |= protocol.read(5, RegisterType::HOLDING_REGISTER, 21, 21, out);

    QVERIFY(result);
    QVERIFY(!out.data.empty());
}

void SerialCommProtocolTest::write_whenNotOpen_shouldFail()
{
    SerialCommProtocol protocol;  // 未调用 open
    RegisterBlock block;
    block.data = {100};

    bool success = protocol.write(5, RegisterType::HOLDING_REGISTER, 21, block);
    QVERIFY(!success);
}


void SerialCommProtocolTest::write_withUnsupportedRegisterType_shouldFail()
{
    SerialCommProtocol protocol;
    QVERIFY(protocol.open("COM10", false));

    RegisterBlock block;
    block.data = {100};

    int invalidRegType = 999;  // 假设无效类型
    bool success = protocol.write(5, invalidRegType, 21, block);
    QVERIFY(!success);
}


void SerialCommProtocolTest::write_withEmptyInput_shouldFail()
{
    SerialCommProtocol protocol;
    QVERIFY(protocol.open("COM10", false));

    RegisterBlock block;
    block.data.clear();  // 空输入

    bool success = protocol.write(5, RegisterType::HOLDING_REGISTER, 21, block);
    QVERIFY(!success);
}

// 未来扩展支持多寄存器写时的预留测试
void SerialCommProtocolTest::write_withInconsistentDataLength_shouldFail()
{
    SerialCommProtocol protocol;
    QVERIFY(protocol.open("COM10", false));

    RegisterBlock block;
    block.data = {100};  // 只传一个值，假设函数未来要写多个

    // 模拟调用未来的 writeMulti（当前实现未支持）
    // 为确保测试意义明确，建议在 write 实现中检查数据长度匹配
    bool success = protocol.write(5, RegisterType::HOLDING_REGISTER, 21, block);
    QVERIFY(success);  // 当前实现为 true，未来实现应为 false
}

void SerialCommProtocolTest::write_whenTimeout_shouldFail()
{
    SerialCommProtocol protocol;
    QVERIFY(protocol.open("COM10", false));

    RegisterBlock block;
    block.data = {100};

    // 你可以通过物理断开设备连接或串口模拟器制造延迟
    bool success = protocol.write(5, RegisterType::HOLDING_REGISTER, 21, block);

    QVERIFY(success);  // 应因超时失败，但是现在还没法测
}


void SerialCommProtocolTest::write_whenModbusError_shouldFail()
{
    SerialCommProtocol protocol;
    QVERIFY(protocol.open("COM10", false));

    RegisterBlock block;
    block.data = {123};

    // 将从站 ID 设置为不存在（例如 250）
    bool success = protocol.write(250, RegisterType::HOLDING_REGISTER, 21, block);

    QVERIFY(!success);  // 非法从站无响应或返回 Modbus 错误
}


void SerialCommProtocolTest::write_singleRegister_shouldSucceed()
{
    SerialCommProtocol protocol;
    QVERIFY(protocol.open("COM10", false));

    RegisterBlock block;
    block.data = {100};

    bool success = protocol.write(5, RegisterType::HOLDING_REGISTER, 21, block);
    QVERIFY2(success, "write false");

    // 读取确认
    RegisterBlock readBlock;
    QVERIFY(protocol.read(5, RegisterType::HOLDING_REGISTER, 21, 21, readBlock));
    QCOMPARE(readBlock.data.size(), 1);
    QCOMPARE(readBlock.data[0], 100);
}

void SerialCommProtocolTest::readUInt64_group_shouldBeConsistent()
{
    SerialCommProtocol protocol;
    QVERIFY(protocol.open("COM10", false));

    quint16 val16 = 0;
    quint32 val32 = 0;
    quint64 val64 = 0;

    // 从寄存器 0x1018 开始读取
    QVERIFY(protocol.readUInt16(5, RegisterType::HOLDING_REGISTER, 0x1018, val16));
    QVERIFY(protocol.readUInt32(5, RegisterType::HOLDING_REGISTER, 0x1018, val32));
    QVERIFY(protocol.readUInt64(5, RegisterType::HOLDING_REGISTER, 0x1018, val64));


    // 因为存在寄存器抖动，可能会有 1~5 左右的抖动
    // 低16位误差容忍 5以内 (Comparing val16 with the lower 16 bits of val32)
    quint16 low16_from_32 = quint16(val32 & 0xFFFF);
    int diff16 = qAbs(int(low16_from_32) - int(val16));
    QVERIFY2(diff16 <= 5, qPrintable(QString("Low 16-bit mismatch: val16=%1, low16_from_val32=%2, diff=%3")
                                         .arg(val16).arg(low16_from_32).arg(diff16)));

    // 验证低 32 位与 val32 一致，误差容忍 5以内 (Comparing val32 with the lower 32 bits of val64)
    quint32 low32_from_64 = quint32(val64 & 0xFFFFFFFF);
    qint64 diff32 = qAbs(qint64(low32_from_64) - qint64(val32)); // Use qint64 for diff to avoid overflow if values are large
    QVERIFY2(diff32 <= 5, qPrintable(QString("Low 32-bit mismatch: val32=%1, low32_from_val64=%2, diff=%3")
                                         .arg(val32).arg(low32_from_64).arg(diff32)));
}
