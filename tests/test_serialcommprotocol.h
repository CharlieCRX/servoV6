#ifndef TEST_SERIALCOMMPROTOCOL_H
#define TEST_SERIALCOMMPROTOCOL_H

#include <QObject>
#include <QTest>
#include "SerialCommProtocol.h" // 包含你正在测试的类

// 测试类必须继承自 QObject
class SerialCommProtocolTest : public QObject
{
    Q_OBJECT // 宏，让 Qt 的元对象系统处理信号和槽，以及 QTest 的私有槽

private slots:
    // 测试用例的声明。这些函数会在运行测试时被 QTest 框架自动发现并执行。
    void defaultConstructor_shouldSetParametersCorrectly();
    void open_withInvalidPort_shouldFail();
};

#endif // TEST_SERIALCOMMPROTOCOL_H
