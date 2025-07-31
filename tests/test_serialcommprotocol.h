#ifndef TEST_SERIALCOMMPROTOCOL_H
#define TEST_SERIALCOMMPROTOCOL_H

#include <QObject>
#include <QTest>

// 测试类必须继承自 QObject
class SerialCommProtocolTest : public QObject
{
    Q_OBJECT // 宏，让 Qt 的元对象系统处理信号和槽，以及 QTest 的私有槽

private slots:
    // 测试用例的声明。这些函数会在运行测试时被 QTest 框架自动发现并执行。
    void defaultConstructor_shouldSetParametersCorrectly();
    void open_withInvalidPort_shouldFail();

    void read_whenNotOpen_shouldFail();                        // 测试在串口未打开时调用 read 是否失败
    void read_withInvalidRegisterRange_shouldFail();           // 起始地址 > 结束地址
    void read_withUnsupportedRegisterType_shouldFail();        // 非法的 regType 值
    void read_withConnectedClientButTimeout_shouldFail();      // 模拟已连接但超时
    void read_withConnectedClientAndValidData_shouldSucceed(); // 成功路径（需模拟或虚拟串口）

    void write_whenNotOpen_shouldFail();                      // 串口未连接
    void write_withUnsupportedRegisterType_shouldFail();      // 非法 regType
    void write_withEmptyInput_shouldFail();                   // in.data 为空
    void write_withInconsistentDataLength_shouldFail();       // 写多个寄存器但数据不足
    void write_whenTimeout_shouldFail();                      // 模拟超时
    void write_whenModbusError_shouldFail();                  // QModbusReply 返回错误
    void write_singleRegister_shouldSucceed();                // 写一个寄存器（正常）
};

#endif // TEST_SERIALCOMMPROTOCOL_H
