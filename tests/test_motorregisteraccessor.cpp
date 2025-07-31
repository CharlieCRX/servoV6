#include "test_motorregisteraccessor.h"
#include <QDebug>

void MotorRegisterAccessorTest::initTestCase()
{
    bool ok = m_protocol.open(m_deviceName.toStdString(), true);
    QVERIFY2(ok, "串口打开失败");

    m_accessor = new MotorRegisterAccessor(&m_protocol);
}

void MotorRegisterAccessorTest::cleanupTestCase()
{
    if (m_accessor) {
        delete m_accessor;
        m_accessor = nullptr;
    }
    m_protocol.close();
}

void MotorRegisterAccessorTest::read_position_shouldSucceed()
{
    uint64_t pos = 0;
    bool ok = m_accessor->readReg64(m_motorID, 0x1018, pos);
    QVERIFY2(ok, "读取位置失败");
    qDebug() << "当前电机位置（64位）：" << QString("0x%1").arg(pos, 0, 16);
}

void MotorRegisterAccessorTest::read_speed_shouldSucceed()
{
    uint16_t rpm = 0;
    bool ok = m_accessor->readReg16(m_motorID, 21, rpm);
    QVERIFY2(ok, "读取速度失败");
    QVERIFY2(rpm <= 200, "速度值超过合理范围");
    qDebug() << "当前转速：" << rpm << "rpm";
}

void MotorRegisterAccessorTest::write_speed_shouldSucceed()
{
    uint16_t targetRPM = 100;
    bool ok = m_accessor->writeReg16(m_motorID, 21, targetRPM);
    QVERIFY2(ok, "写入速度失败");

    uint16_t readBack = 0;
    ok = m_accessor->readReg16(m_motorID, 21, readBack);
    QVERIFY2(ok, "写回读取失败");
    QCOMPARE(readBack, targetRPM);
    qDebug() << "成功写入并验证转速：" << readBack << "rpm";
}
