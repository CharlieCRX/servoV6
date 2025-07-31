#ifndef TEST_MOTORREGISTERACCESSOR_H
#define TEST_MOTORREGISTERACCESSOR_H

#include <QObject>
#include <QTest>
#include "MotorRegisterAccessor.h"
#include "SerialCommProtocol.h" // 你真实使用的通信协议类

class MotorRegisterAccessorTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();   // 在所有测试前运行（用于初始化串口）
    void cleanupTestCase(); // 所有测试后运行（释放资源）

    void read_position_shouldSucceed();    // 测试读取电机位置
    void read_speed_shouldSucceed();       // 测试读取电机速度
    void write_speed_shouldSucceed();      // 写入一个转速值

private:
    SerialCommProtocol m_protocol;
    MotorRegisterAccessor* m_accessor = nullptr;

    const int m_motorID = 5;
    const QString m_deviceName = "COM10"; // 根据实际串口修改
};

#endif // TEST_MOTORREGISTERACCESSOR_H
